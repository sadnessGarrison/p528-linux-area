#include <dlfcn.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <cmath>
#include <vector>
#include "P528LinuxHvD.h"
#include "../submodules/p528-linux/include/p528.h"

// Local globals
void* hLib = nullptr;
p528func libP528 = nullptr;

char buf[TIME_SIZE];

/*=============================================================================
 |
 |  Description:  Main function of the p528-hvd driver executable
 |
 *===========================================================================*/
int main(int argc, char** argv) {
    int rtn;
    HvDParams params;

    time_t t = time(NULL);
    if (ctime_r(&t, buf) == NULL)
        buf[0] = '\0';

    rtn = ParseArguments(argc, argv, &params);
    if (rtn != SUCCESS) {
        if (rtn != P528__RETURN_SUCCESS)
            Help();
        return rtn;
    }

    if (params.mode == MODE_VERSION) {
        printf("p528-hvd\n");
        return SUCCESS;
    }

    rtn = ValidateInputs(&params);
    if (rtn) {
        printf("\n");
        Help();
        return rtn;
    }

    rtn = LoadLibrary();
    if (rtn)
        return rtn;

    return RunHvD(&params);
}

/*=============================================================================
 |
 |  Description:  For each distance in [start_dist, end_dist], find the
 |                minimum h2 at which the P.528 basic transmission loss
 |                equals target_A__db (bisection over h2 in [1.5, 20 000] m)
 |
 |        Input:  params        - Structure with user input parameters
 |
 |      Returns:  SUCCESS, SUCCESS_WITH_WARNINGS, or error code
 |
 *===========================================================================*/
int RunHvD(HvDParams* params) {
    int num_points = static_cast<int>(floor((params->end_dist__km - params->start_dist__km) / params->distint__km)) + 1;
    if (num_points <= 0)
        return HVDERR__INVALID_DISTANCE_RANGE;

    HvDData data(num_points);
    printf("Computing equivalent heights for %d distances...\n", num_points);

    int rtn = SUCCESS;

    for (int i = 0; i < num_points; ++i) {
        PrintProgress(i, num_points);

        double d = params->start_dist__km + static_cast<double>(i) * params->distint__km;
        if (d > params->end_dist__km) d = params->end_dist__km;

        double A__db, A_fs__db, A_a__db;
        int prop_mode, warns;
        double h2 = FindEquivalentHeight(d, params->h_1__meter, params->f__mhz,
                                         params->T_pol, params->p, params->target_A__db,
                                         &A__db, &A_fs__db, &A_a__db, &prop_mode, &warns);

        if (warns & HVDWARN__TARGET_NOT_ACHIEVABLE)
            rtn = SUCCESS_WITH_WARNINGS;

        data.distances.push_back(d);
        data.h2_equivs__meter.push_back(h2);
        data.A__dbs.push_back(A__db);
        data.A_fs__dbs.push_back(A_fs__db);
        data.A_a__dbs.push_back(A_a__db);
        data.propagation_modes.push_back(prop_mode);
        data.warnings.push_back(warns);
    }

    printf("\n");
    return WriteResultsToFile(params, data);
}

/*=============================================================================
 |
 |  Description:  Find the minimum h2 at which P.528 basic transmission
 |                loss is <= target_A__db, via bisection over [1.5, 20 000] m.
 |
 |                P.528 loss is not monotonic with h2: it can dip below the
 |                target, rise back above, then fall again at higher altitude.
 |                A wide bisection over the full range would converge on a
 |                false high-altitude root.  Instead, scan upward in coarse
 |                steps to find the first bracket where loss crosses from
 |                above to below the target, then bisect within that bracket.
 |
 |                Returns NAN with HVDWARN__TARGET_NOT_ACHIEVABLE if the loss
 |                at h2 = 20 000 m still exceeds the target.  Returns H2_MIN
 |                if the target is already met at the minimum height.
 |
 |        Input:  d__km             - Path distance (km)
 |                h_1__meter        - Low terminal height (m)
 |                f__mhz            - Frequency (MHz)
 |                T_pol             - Polarization
 |                p                 - Time percentage
 |                target_A__db      - Target basic transmission loss (dB)
 |
 |      Outputs:  achieved_A__db    - Loss at the returned h2
 |                achieved_A_fs__db - Free space loss at the returned h2
 |                achieved_A_a__db  - Atmospheric absorption at the returned h2
 |                prop_mode         - Propagation mode at the returned h2
 |                warns             - Warning flags
 |
 |      Returns:  Minimum h2 (m) achieving the target, or NAN if unachievable
 |
 *===========================================================================*/
double FindEquivalentHeight(double d__km, double h_1__meter, double f__mhz,
                            int T_pol, double p, double target_A__db,
                            double* achieved_A__db, double* achieved_A_fs__db,
                            double* achieved_A_a__db, int* prop_mode, int* warns) {
    // H2_MIN must be >= h_1__meter: P.528 requires h_1 <= h_2 (ERROR_VALIDATION__TERM_GEO)
    const double H2_MIN = (h_1__meter > 1.5) ? h_1__meter : 1.5;
    const double H2_MAX = 20000.0;
    const double TOLERANCE__meter = 0.1;

    Result r = {0};

    // Helper to call libP528 and return a sentinel on validation error
    auto callP528 = [&](double h2, Result* out) -> bool {
        *out = {0};
        int rc = libP528(d__km, h_1__meter, h2, f__mhz, T_pol, p, out);
        return (rc == SUCCESS || rc == SUCCESS_WITH_WARNINGS);
    };

    // Test at minimum height
    if (!callP528(H2_MIN, &r) || r.A__db == 0.0) {
        // P.528 returned an error at this height; try the maximum
        if (!callP528(H2_MAX, &r) || r.A__db == 0.0) {
            *achieved_A__db    = 0;
            *achieved_A_fs__db = 0;
            *achieved_A_a__db  = 0;
            *prop_mode         = 0;
            *warns             = HVDWARN__TARGET_NOT_ACHIEVABLE;
            return NAN;
        }
    }
    if (r.A__db <= target_A__db) {
        // Target already met at minimum height; any altitude works
        *achieved_A__db    = r.A__db;
        *achieved_A_fs__db = r.A_fs__db;
        *achieved_A_a__db  = r.A_a__db;
        *prop_mode         = r.propagation_mode;
        *warns             = r.warnings;
        return H2_MIN;
    }

    // Test at maximum height
    callP528(H2_MAX, &r);
    if (r.A__db > target_A__db) {
        // Target not achievable within P.528 height limits; report best-effort values
        *achieved_A__db    = r.A__db;
        *achieved_A_fs__db = r.A_fs__db;
        *achieved_A_a__db  = r.A_a__db;
        *prop_mode         = r.propagation_mode;
        *warns             = r.warnings | HVDWARN__TARGET_NOT_ACHIEVABLE;
        return NAN;
    }

    // Scan upward in coarse steps to find the first bracket [h2_lo, h2_hi]
    // where loss crosses from above to below the target.  P.528 loss can be
    // non-monotonic (dip below target, rise above, then fall again), so a
    // wide bisection over [H2_MIN, H2_MAX] would miss the minimum root.
    const double SCAN_STEP__meter = 100.0;
    double h2_lo = H2_MIN;   // A(h2_lo) > target (verified above)
    double h2_scan = H2_MIN + SCAN_STEP__meter;

    while (h2_scan <= H2_MAX) {
        callP528(h2_scan, &r);
        if (r.A__db <= target_A__db) {
            // Found first downward crossing; bisect within [h2_lo, h2_scan]
            double h2_hi = h2_scan;
            while (h2_hi - h2_lo > TOLERANCE__meter) {
                double h2_mid = (h2_lo + h2_hi) / 2.0;
                callP528(h2_mid, &r);
                if (r.A__db > target_A__db)
                    h2_lo = h2_mid;
                else
                    h2_hi = h2_mid;
            }
            callP528(h2_hi, &r);
            *achieved_A__db    = r.A__db;
            *achieved_A_fs__db = r.A_fs__db;
            *achieved_A_a__db  = r.A_a__db;
            *prop_mode         = r.propagation_mode;
            *warns             = r.warnings;
            return h2_hi;
        }
        h2_lo = h2_scan;
        h2_scan += SCAN_STEP__meter;
    }

    // H2_MAX check above confirmed a solution exists; return it as fallback
    callP528(H2_MAX, &r);
    *achieved_A__db    = r.A__db;
    *achieved_A_fs__db = r.A_fs__db;
    *achieved_A_a__db  = r.A_a__db;
    *prop_mode         = r.propagation_mode;
    *warns             = r.warnings;
    return H2_MAX;
}

/*=============================================================================
 |
 |  Description:  Write results to CSV file
 |
 |        Input:  params        - Structure with user input parameters
 |                data          - Reference to HvD data structure
 |
 |      Returns:  SUCCESS or error code
 |
 *===========================================================================*/
int WriteResultsToFile(HvDParams* params, const HvDData& data) {
    // Resolve output dir relative to the executable's location
    char exe_path[PATH_MAX] = { 0 };
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    char out_dir[PATH_MAX + 8];
    if (len > 0) {
        exe_path[len] = '\0';
        char* slash = strrchr(exe_path, '/');
        if (slash) *slash = '\0';
        snprintf(out_dir, sizeof(out_dir), "%s/output", exe_path);
    } else {
        strncpy(out_dir, "output", sizeof(out_dir) - 1);
    }
    mkdir(out_dir, 0755);
    const char* basename = strrchr(params->out_file, '/');
    basename = basename ? basename + 1 : params->out_file;
    // Split stem and extension
    const char* dot = strrchr(basename, '.');
    char stem[MAX_FILENAME_LENGTH] = { 0 };
    const char* ext = "";
    if (dot) {
        strncpy(stem, basename, dot - basename);
        ext = dot;
    } else {
        strncpy(stem, basename, sizeof(stem) - 1);
    }
    // Timestamp
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm_info);
    // Frequency
    char freq_str[32];
    snprintf(freq_str, sizeof(freq_str), "%.6g", params->f__mhz);
    char out_path[MAX_FILENAME_LENGTH + 64];
    snprintf(out_path, sizeof(out_path), "%s/%s_%s_%sMHz%s", out_dir, stem, ts, freq_str, ext);
    FILE* fp = fopen(out_path, "w");
    if (!fp) {
        printf("Error opening output file (%s). Exiting.\n", out_path);
        return HVDERR__VALIDATION_OUT_FILE;
    }

    fprintf(fp, "Date Generated,%s", buf);
    fprintf(fp, "Inputs\n");
    fprintf(fp, "h_1__meter,%.3f\n", params->h_1__meter);
    fprintf(fp, "f__mhz,%.3f\n", params->f__mhz);
    fprintf(fp, "p,%.3f\n", params->p);
    fprintf(fp, "T_pol,%d\n", params->T_pol);
    fprintf(fp, "target_A__db,%.3f\n", params->target_A__db);
    fprintf(fp, "start_dist__km,%.3f\n", params->start_dist__km);
    fprintf(fp, "end_dist__km,%.3f\n", params->end_dist__km);
    fprintf(fp, "distint__km,%.3f\n", params->distint__km);
    fprintf(fp, "\n");

    fprintf(fp, "Results\n");
    fprintf(fp, "Distance (km),Equivalent Height H2 (m),Propagation Mode,Free Space Loss (dB),Atmospheric Absorption (dB),Basic Transmission Loss (dB),Warnings\n");

    for (size_t i = 0; i < data.distances.size(); ++i) {
        const char* mode_str;
        switch (data.propagation_modes[i]) {
            case 1:  mode_str = "LOS";          break;
            case 2:  mode_str = "Diffraction";  break;
            case 3:  mode_str = "Troposcatter"; break;
            default: mode_str = "Unknown";      break;
        }

        if (std::isnan(data.h2_equivs__meter[i])) {
            fprintf(fp, "%.3f,>20000,%s,%.3f,%.3f,%.3f,0x%x\n",
                    data.distances[i], mode_str,
                    data.A_fs__dbs[i], data.A_a__dbs[i], data.A__dbs[i],
                    data.warnings[i]);
        } else {
            fprintf(fp, "%.3f,%.3f,%s,%.3f,%.3f,%.3f,0x%x\n",
                    data.distances[i], data.h2_equivs__meter[i], mode_str,
                    data.A_fs__dbs[i], data.A_a__dbs[i], data.A__dbs[i],
                    data.warnings[i]);
        }
    }

    fclose(fp);
    printf("Results written to %s\n", out_path);
    return SUCCESS;
}

/*=============================================================================
 |
 |  Description:  Print progress indicator
 |
 |        Input:  current       - Current progress
 |                total         - Total items
 |
 |      Returns:  [void]
 |
 *===========================================================================*/
void PrintProgress(int current, int total) {
    if (total <= 0) return;

    static int last_percent = -1;
    int percent = (current * 100) / total;

    if (percent != last_percent && percent % 10 == 0) {
        printf("%d%% ", percent);
        fflush(stdout);
        last_percent = percent;
    }
}

/*=============================================================================
 |
 |  Description:  Loads the P.528 library shared object and resolves
 |                function pointers
 |
 |        Input:  [void]
 |
 |      Returns:  SUCCESS or error code
 |
 *===========================================================================*/
int LoadLibrary() {
    const char* candidates[] = {"libp528.so", "p528.so", "./libp528.so", "apps/libp528.so", NULL};
    const char** c = candidates;
    while (*c) {
        hLib = dlopen(*c, RTLD_NOW);
        if (hLib) break;
        c++;
    }

    if (!hLib) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return HVDERR__LIBRARY_LOADING;
    }

    dlerror();
    libP528 = (p528func)dlsym(hLib, "P528");
    char* err = dlerror();
    if (err != NULL || libP528 == nullptr) {
        fprintf(stderr, "dlsym(P528) failed: %s\n", err ? err : "unknown");
        dlclose(hLib);
        hLib = nullptr;
        return HVDERR__GETP528_FUNC_LOADING;
    }

    return SUCCESS;
}

/*=============================================================================
 |
 |  Description:  Helper function to format and print error messages
 |                encountered during command argument parsing
 |
 |        Input:  opt       - Command flag in error
 |                err       - Error code
 |
 |      Returns:  Error code
 |
 *===========================================================================*/
int ParseErrorMsgHelper(const char* opt, int err) {
    printf("HvDErr %i: Unable to parse %s value.\n", err, opt);
    return err;
}

/*=============================================================================
 |
 |  Description:  Parse the command line arguments
 |
 |        Input:  argc      - Number of arguments
 |                argv      - Command line arguments
 |
 |       Output:  params    - Structure with user input parameters
 |
 |      Returns:  SUCCESS, or error code encountered
 |
 *===========================================================================*/
int ParseArguments(int argc, char** argv, HvDParams* params) {
    for (int i = 1; i < argc; i++) {
        Lowercase(argv[i]);

        if (Match("-h1", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->h_1__meter)) != 1)
                return ParseErrorMsgHelper("-h1 [height]", HVDERR__PARSE_H1_HEIGHT);
            i++;
        }
        else if (Match("-f", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->f__mhz)) != 1)
                return ParseErrorMsgHelper("-f [frequency]", HVDERR__PARSE_F_FREQUENCY);
            i++;
        }
        else if (Match("-p", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->p)) != 1)
                return ParseErrorMsgHelper("-p [percentage]", HVDERR__PARSE_P_PERCENTAGE);
            i++;
        }
        else if (Match("-tpol", argv[i])) {
            if (sscanf(argv[i + 1], "%i", &(params->T_pol)) != 1)
                return ParseErrorMsgHelper("-tpol [polarization]", HVDERR__PARSE_TPOL_POLARIZATION);
            i++;
        }
        else if (Match("-targetloss", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->target_A__db)) != 1)
                return ParseErrorMsgHelper("-targetloss [loss]", HVDERR__PARSE_TARGET_LOSS);
            i++;
        }
        else if (Match("-startdist", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->start_dist__km)) != 1)
                return ParseErrorMsgHelper("-startdist [distance]", HVDERR__PARSE_D_DISTANCE);
            i++;
        }
        else if (Match("-enddist", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->end_dist__km)) != 1)
                return ParseErrorMsgHelper("-enddist [distance]", HVDERR__PARSE_D_DISTANCE);
            i++;
        }
        else if (Match("-distint", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->distint__km)) != 1)
                return ParseErrorMsgHelper("-distint [interval]", HVDERR__PARSE_D_DISTANCE);
            i++;
        }
        else if (Match("-o", argv[i])) {
            strncpy(params->out_file, argv[i + 1], sizeof(params->out_file) - 1);
            params->out_file[sizeof(params->out_file) - 1] = '\0';
            i++;
        }
        else if (Match("-v", argv[i])) {
            params->mode = MODE_VERSION;
            return SUCCESS;
        }
        else if (Match("-h", argv[i])) {
            Help();
            return P528__RETURN_SUCCESS;
        }
        else {
            printf("Unknown option: %s\n", argv[i]);
            return HVDERR__INVALID_OPTION;
        }
    }
    return SUCCESS;
}

/*=============================================================================
 |
 |  Description:  Validate that the required inputs are present and within
 |                acceptable ranges
 |
 |        Input:  params        - Structure with user input parameters
 |
 |      Returns:  SUCCESS, or error code encountered
 |
 *===========================================================================*/
int ValidateInputs(HvDParams* params) {
    if (params->h_1__meter == NOT_SET)
        return Validate_RequiredErrMsgHelper("-h1", HVDERR__VALIDATION_H1);
    if (params->f__mhz == NOT_SET)
        return Validate_RequiredErrMsgHelper("-f", HVDERR__VALIDATION_F);
    if (params->p == NOT_SET)
        return Validate_RequiredErrMsgHelper("-p", HVDERR__VALIDATION_P);
    if (params->T_pol == NOT_SET)
        return Validate_RequiredErrMsgHelper("-tpol", HVDERR__VALIDATION_P);
    if (params->target_A__db == NOT_SET)
        return Validate_RequiredErrMsgHelper("-targetloss", HVDERR__VALIDATION_TARGET_LOSS);
    if (params->start_dist__km == NOT_SET)
        return Validate_RequiredErrMsgHelper("-startdist", HVDERR__VALIDATION_D);
    if (params->end_dist__km == NOT_SET)
        return Validate_RequiredErrMsgHelper("-enddist", HVDERR__VALIDATION_D);
    if (params->distint__km == NOT_SET)
        return Validate_RequiredErrMsgHelper("-distint", HVDERR__VALIDATION_D);
    if (strlen(params->out_file) == 0)
        return Validate_RequiredErrMsgHelper("-o", HVDERR__VALIDATION_OUT_FILE);

    if (params->h_1__meter < 1.5 || params->h_1__meter > 20000) {
        printf("HvDError %i: Low terminal height must be between 1.5 and 20000 meters\n", HVDERR__VALIDATION_H1);
        return HVDERR__VALIDATION_H1;
    }
    if (params->f__mhz < 100 || params->f__mhz > 30000) {
        printf("HvDError %i: Frequency must be between 100 and 30000 MHz\n", HVDERR__VALIDATION_F);
        return HVDERR__VALIDATION_F;
    }
    if (params->p < 1 || params->p > 99) {
        printf("HvDError %i: Time percentage must be between 1 and 99\n", HVDERR__VALIDATION_P);
        return HVDERR__VALIDATION_P;
    }
    if (params->T_pol != 0 && params->T_pol != 1) {
        printf("HvDError %i: Polarization must be 0 (horizontal) or 1 (vertical)\n", HVDERR__VALIDATION_P);
        return HVDERR__VALIDATION_P;
    }
    if (params->target_A__db <= 0) {
        printf("HvDError %i: Target basic transmission loss must be positive (dB)\n", HVDERR__VALIDATION_TARGET_LOSS);
        return HVDERR__VALIDATION_TARGET_LOSS;
    }
    if (params->start_dist__km <= 0 || params->end_dist__km <= 0) {
        printf("HvDError %i: Distances must be positive\n", HVDERR__VALIDATION_D);
        return HVDERR__VALIDATION_D;
    }
    if (params->end_dist__km < params->start_dist__km) {
        printf("HvDError %i: End distance must be greater than or equal to start distance\n", HVDERR__INVALID_DISTANCE_RANGE);
        return HVDERR__INVALID_DISTANCE_RANGE;
    }
    if (params->distint__km <= 0) {
        printf("HvDError %i: Distance interval must be positive\n", HVDERR__VALIDATION_D);
        return HVDERR__VALIDATION_D;
    }

    return SUCCESS;
}

/*=============================================================================
 |
 |  Description:  Helper function to format and print error messages
 |                encountered during validation of input parameters
 |
 |        Input:  opt       - Command flag in error
 |                err       - Error code
 |
 |      Returns:  Error code
 |
 *===========================================================================*/
int Validate_RequiredErrMsgHelper(const char* opt, int err) {
    printf("HvDError %i: Option %s is required but was not provided\n", err, opt);
    return err;
}

/*=============================================================================
 |
 |  Description:  Convert the char array to lower case
 |
 | Input/Output:  argv      - value
 |
 |      Returns:  [Void]
 |
 *===========================================================================*/
void Lowercase(char* argv) {
    for (int i = 0; i < (int)strlen(argv); i++)
        argv[i] = tolower(argv[i]);
}

/*=============================================================================
 |
 |  Description:  Compare two strings to see if they match
 |
 |        Input:  opt       - Given char array
 |                arg       - Expected char array
 |
 |      Returns:  True/False
 |
 *===========================================================================*/
bool Match(const char* opt, char* arg) {
    return strcmp(opt, arg) == 0;
}

/*=============================================================================
 |
 |  Description:  Print help instructions to the terminal
 |
 |        Input:  [Void]
 |
 |      Returns:  [Void]
 |
 *===========================================================================*/
void Help() {
    printf("\n");
    printf("Usage: p528-hvd [Options]\n");
    printf("Options (not case sensitive)\n");
    printf("\t-h          :: Displays help\n");
    printf("\t-v          :: Displays version information\n");
    printf("\t-h1         :: Height of low (ground) terminal, in meters\n");
    printf("\t-f          :: Frequency, in MHz\n");
    printf("\t-p          :: Time percentage (availability)\n");
    printf("\t-tpol       :: Polarization (0=horizontal, 1=vertical)\n");
    printf("\t-targetloss :: Target basic transmission loss, in dB\n");
    printf("\t-startdist  :: Starting distance, in km (must be > 0)\n");
    printf("\t-enddist    :: Ending distance, in km\n");
    printf("\t-distint    :: Distance interval between points, in km\n");
    printf("\t-o          :: Output file name\n");
    printf("\n");
    printf("For each distance, finds the minimum height of the airborne terminal (h2)\n");
    printf("at which the P.528 basic transmission loss equals the target loss.\n");
    printf("If the required height exceeds 20000 m, the row shows '>20000' and\n");
    printf("sets warning flag 0x10 (HVDWARN__TARGET_NOT_ACHIEVABLE).\n");
    printf("\n");
    printf("Example:\n");
    printf("\tp528-hvd -h1 2 -f 450 -p 95 -tpol 1 -targetloss 150 -startdist 10 -enddist 100 -distint 1 -o equivalent.csv\n");
    printf("\n");
}
