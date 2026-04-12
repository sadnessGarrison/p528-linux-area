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
#include "P528LinuxArea.h"
#include "../submodules/p528-linux/include/p528.h"

/*=============================================================================
 |
 |  Description:  This program allows the user to execute the P.528 model
 |                library and generate results.  For full details and examples
 |                on how to use it, please see the readme.txt file.
 |
 *===========================================================================*/

// Local globals
void* hLib = nullptr;
p528func libP528 = nullptr;

char buf[TIME_SIZE];

/*=============================================================================
 |
 |  Description:  Main function of the P.528 driver executable
 |
 *===========================================================================*/
int main(int argc, char** argv) {
    int rtn;
    P528Params params;

    // Get the time
    time_t t = time(NULL);
    if (ctime_r(&t, buf) == NULL) {
        buf[0] = '\0';
    }

    rtn = ParseArguments(argc, argv, &params);
    if (rtn != SUCCESS) {
        if (rtn != P528__RETURN_SUCCESS) {
            Help();
        }
        return rtn;
    }

    if (params.mode != MODE_VERSION) {
        rtn = ValidateInputs(&params);
        if (rtn)
        {
            printf("\n");
            Help();
            return rtn;
        }
    }

    rtn = LoadLibrary();
    if (rtn)
        return rtn;

    rtn = CallP528_CURVE(&params);
}


/*=============================================================================
 |
 |  Description:  Generates data points for a P.528 loss-vs-distance curve
 |
 |        Input:  params        - Structure with user input parameters
 |
 |      Returns:  P.528 library return code
 |
 *===========================================================================*/
int CallP528_CURVE(P528Params* params) {
    // Calculate number of points by stepping h2 from h2_start to h2_end.
    // Distance at each point is derived linearly from height.
    int num_points = static_cast<int>(floor((params->h2_end__meter - params->h2_start__meter) / params->heightint__meter)) + 1;
    if (num_points <= 0) {
        return P528ERR__INVALID_HEIGHT_RANGE;
    }

    TrajectoryData data(num_points);
    
    int rtn = CalculateTrajectoryPoints(params, data);
    if (rtn != SUCCESS) {
        return rtn;
    }
    
    rtn = ComputePathLosses(params, data);
    if (rtn != SUCCESS) {
        return rtn;
    }
    
    return WriteResultsToFile(params, data);
}

/*=============================================================================
 |
 |  Description:  Calculate trajectory points for the simulation
 |
 |        Input:  params        - Structure with user input parameters
 |                data          - Reference to trajectory data structure
 |
 |      Returns:  SUCCESS or error code
 |
 *===========================================================================*/
int CalculateTrajectoryPoints(P528Params* params, TrajectoryData& data) {
    double height_range__meter = params->h2_end__meter - params->h2_start__meter;
    size_t num_points = static_cast<size_t>(floor(height_range__meter / params->heightint__meter)) + 1;

    for (size_t i = 0; i < num_points; ++i) {
        double h2 = params->h2_start__meter + static_cast<double>(i) * params->heightint__meter;
        if (h2 > params->h2_end__meter) h2 = params->h2_end__meter;

        double t = (h2 - params->h2_start__meter) / params->ascent_rate__mps;

        // Distance varies linearly with height; collapses to startdist when start==end
        double d = params->start_dist__km
                 + (params->end_dist__km - params->start_dist__km)
                 * (h2 - params->h2_start__meter) / height_range__meter;

        data.times.push_back(t);
        data.heights.push_back(h2);
        data.distances.push_back(d);
    }

    return SUCCESS;
}

/*=============================================================================
 |
 |  Description:  Compute path losses for each trajectory point
 |
 |        Input:  params        - Structure with user input parameters
 |                data          - Reference to trajectory data structure
 |
 |      Returns:  SUCCESS or error code
 |
 *===========================================================================*/
int ComputePathLosses(P528Params* params, TrajectoryData& data) {
    printf("Computing path losses for %zu points...\n", data.times.size());
    
    int rtn = SUCCESS;
    
    for (size_t i = 0; i < data.times.size(); ++i) {
        PrintProgress(static_cast<int>(i), static_cast<int>(data.times.size()));

        Result result = {0};

        int p528_rtn = libP528(data.distances[i], params->h_1__meter, data.heights[i],
                               params->f__mhz, params->T_pol, params->p, &result);
        if (p528_rtn != SUCCESS && p528_rtn != SUCCESS_WITH_WARNINGS) {
            printf("\nP528 error %i at point %zu (d=%.3f km, h2=%.3f m)\n",
                   p528_rtn, i, data.distances[i], data.heights[i]);
            return p528_rtn;
        }
        if (p528_rtn == SUCCESS_WITH_WARNINGS)
            rtn = SUCCESS_WITH_WARNINGS;

        // Direct ray length (slant path distance), in km
        double delta_h__km = (data.heights[i] - params->h_1__meter) / METERS_TO_KM;
        double r_0__km = sqrt(data.distances[i] * data.distances[i] + delta_h__km * delta_h__km);

        data.A__dbs.push_back(result.A__db);
        data.A_fs__dbs.push_back(result.A_fs__db);
        data.A_a__dbs.push_back(result.A_a__db);
        data.A_excess__dbs.push_back(result.A__db - result.A_fs__db - result.A_a__db);
        data.r_0__kms.push_back(r_0__km);
        data.propagation_modes.push_back(result.propagation_mode);
        data.warnings.push_back(result.warnings);
    }
    
    printf("\n"); // New line after progress
    return rtn;
}

/*=============================================================================
 |
 |  Description:  Write results to CSV file
 |
 |        Input:  params        - Structure with user input parameters
 |                data          - Reference to trajectory data structure
 |
 |      Returns:  SUCCESS or error code
 |
 *===========================================================================*/
int WriteResultsToFile(P528Params* params, const TrajectoryData& data) {
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
    char out_path[PATH_MAX + 2 * MAX_FILENAME_LENGTH + 80];
    snprintf(out_path, sizeof(out_path), "%s/%s_%s_%sMHz%s", out_dir, stem, ts, freq_str, ext);
    FILE* fp = fopen(out_path, "w");
    if (!fp) {
        printf("Error opening output file (%s). Exiting.\n", out_path);
        return P528ERR__VALIDATION_OUT_FILE;
    }
    
    // Write header information
    fprintf(fp, "Date Generated,%s", buf);
    fprintf(fp, "Inputs\n");
    fprintf(fp, "h_1__meter,%.3f\n", params->h_1__meter);
    fprintf(fp, "h2_start__meter,%.3f\n", params->h2_start__meter);
    fprintf(fp, "h2_end__meter,%.3f\n", params->h2_end__meter);
    fprintf(fp, "ascent_rate__mps,%.3f\n", params->ascent_rate__mps);
    fprintf(fp, "start_dist__km,%.3f\n", params->start_dist__km);
    fprintf(fp, "end_dist__km,%.3f\n", params->end_dist__km);
    fprintf(fp, "heightint__meter,%.3f\n", params->heightint__meter);
    fprintf(fp, "f__mhz,%.3f\n", params->f__mhz);
    fprintf(fp, "p,%.3f\n", params->p);
    fprintf(fp, "T_pol,%i\n", params->T_pol);
    fprintf(fp, "\n");
    
    // Write results header
    fprintf(fp, "Results\n");
    fprintf(fp, "Time (s),Height Terminal 2 (m),Distance (km),Slant Path Distance (km),Propagation Mode,Free Space Loss (dB),Atmospheric Absorption (dB),Path Correction + Variability (dB),Basic Transmission Loss (dB),Warnings\n");
    
    // Write data points
    for (size_t i = 0; i < data.times.size(); ++i) {
        const char* mode_str;
        switch (data.propagation_modes[i]) {
            case 1:  mode_str = "LOS";          break;
            case 2:  mode_str = "Diffraction";  break;
            case 3:  mode_str = "Troposcatter"; break;
            default: mode_str = "Unknown";      break;
        }
        fprintf(fp, "%.3f,%.3f,%.3f,%.3f,%s,%.3f,%.3f,%.3f,%.3f,0x%x\n",
                data.times[i], data.heights[i], data.distances[i],
                data.r_0__kms[i], mode_str,
                data.A_fs__dbs[i], data.A_a__dbs[i], data.A_excess__dbs[i],
                data.A__dbs[i], data.warnings[i]);
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
    // Try common shared object names
    const char* candidates[] = {"libp528.so", "p528.so", "./libp528.so", "apps/libp528.so", NULL};
    const char** c = candidates;
    while (*c) {
        hLib = dlopen(*c, RTLD_NOW);
        if (hLib)
            break;
        c++;
    }

    if (!hLib) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return P528ERR__LIBRARY_LOADING;
    }

    // Resolve the symbol P528 (must be exported with C linkage)
    dlerror();
    libP528 = (p528func)dlsym(hLib, "P528");
    char* err = dlerror();
    if (err != NULL || libP528 == nullptr) {
        fprintf(stderr, "dlsym(P528) failed: %s\n", err ? err : "unknown");
        dlclose(hLib);
        hLib = nullptr;
        return P528ERR__GETP528_FUNC_LOADING;
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
    printf("P528Err %i: Unable to parse %s value.\n", err, opt);
    return err;
}

/*=============================================================================
 |
 |  Description:  Parse the command line arguments
 |
 |        Input:  argc      - Number of arguments
 |                argv      - Command line arguments
 |
 |       Output:
 |                params    - Structure with user input parameters
 |
 |      Returns:  SUCCESS, or error code encountered
 |
 *===========================================================================*/
int ParseArguments(int argc, char** argv, P528Params* params)
{
    for (int i = 1; i < argc; i++) {
        Lowercase(argv[i]);

        if (Match("-h1", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->h_1__meter)) != 1)
                return ParseErrorMsgHelper("-h1 [height]", P528ERR__PARSE_H1_HEIGHT);
            i++;
        }
        else if (Match("-h2start", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->h2_start__meter)) != 1)
                return ParseErrorMsgHelper("-h2start [height]", P528ERR__PARSE_H2_HEIGHT);
            i++;
        }
        else if (Match("-h2end", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->h2_end__meter)) != 1)
                return ParseErrorMsgHelper("-h2end [height]", P528ERR__PARSE_H2_HEIGHT);
            i++;
        }
        else if (Match("-ascent", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->ascent_rate__mps)) != 1)
                return ParseErrorMsgHelper("-ascent [rate]", P528ERR__PARSE_H2_HEIGHT);
            i++;
        }
        else if (Match("-startdist", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->start_dist__km)) != 1)
                return ParseErrorMsgHelper("-startdist [distance]", P528ERR__PARSE_D_DISTANCE);
            i++;
        }
        else if (Match("-enddist", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->end_dist__km)) != 1)
                return ParseErrorMsgHelper("-enddist [distance]", P528ERR__PARSE_D_DISTANCE);
            i++;
        }
        else if (Match("-heightint", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->heightint__meter)) != 1)
                return ParseErrorMsgHelper("-heightint [interval]", P528ERR__PARSE_D_DISTANCE);
            i++;
        }
        else if (Match("-f", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->f__mhz)) != 1)
                return ParseErrorMsgHelper("-f [frequency]", P528ERR__PARSE_F_FREQUENCY);
            i++;
        }

        else if (Match("-p", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->p)) != 1)
                return ParseErrorMsgHelper("-p [percentage]", P528ERR__PARSE_P_PERCENTAGE);
            i++;
        }
        else if (Match("-tpol", argv[i])) {
            if (sscanf(argv[i + 1], "%i", &(params->T_pol)) != 1)
                return ParseErrorMsgHelper("-tpol [polarization]", P528ERR__PARSE_TPOL_POLARIZATION);
            i++;
        }
        else if (Match("-o", argv[i])) {
            strncpy(params->out_file, argv[i + 1], sizeof(params->out_file)-1);
            params->out_file[sizeof(params->out_file)-1] = '\0';
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
            return P528ERR__INVALID_OPTION;
        }
    }

    return SUCCESS;
}

/*=============================================================================
 |
 |  Description:  Validate that the required inputs are present for the
 |                mode specified by the user.  This function DOES NOT
 |                check the validity of the parameter values - only that
 |                required parameters have been specified by the user
 |
 |        Input:  params        - Structure with user input parameters
 |
 |      Returns:  SUCCESS, or error code encountered
 |
 *===========================================================================*/
int ValidateInputs(P528Params* params) {
    // Check required parameters are set
    if (params->f__mhz == NOT_SET)
        return Validate_RequiredErrMsgHelper("-f", P528ERR__VALIDATION_F);

    if (params->p == NOT_SET)
        return Validate_RequiredErrMsgHelper("-p", P528ERR__VALIDATION_P);

    if (params->T_pol == NOT_SET)
        return Validate_RequiredErrMsgHelper("-tpol", P528ERR__VALIDATION_P);

    if (params->h_1__meter == NOT_SET)
        return Validate_RequiredErrMsgHelper("-h1", P528ERR__VALIDATION_H1);

    if (params->h2_start__meter == NOT_SET)
        return Validate_RequiredErrMsgHelper("-h2start", P528ERR__VALIDATION_H2);
    if (params->h2_end__meter == NOT_SET)
        return Validate_RequiredErrMsgHelper("-h2end", P528ERR__VALIDATION_H2);
    if (params->ascent_rate__mps == NOT_SET)
        return Validate_RequiredErrMsgHelper("-ascent", P528ERR__VALIDATION_H2);
    if (params->start_dist__km == NOT_SET)
        return Validate_RequiredErrMsgHelper("-startdist", P528ERR__VALIDATION_D);
    if (params->end_dist__km == NOT_SET)
        return Validate_RequiredErrMsgHelper("-enddist", P528ERR__VALIDATION_D);
    if (strlen(params->out_file) == 0)
        return  Validate_RequiredErrMsgHelper("-o", P528ERR__VALIDATION_OUT_FILE);

    if (params->heightint__meter == NOT_SET)
        return Validate_RequiredErrMsgHelper("-heightint", P528ERR__VALIDATION_D);
    if (params->heightint__meter <= 0) {
        printf("P528Error %i: Height interval must be positive\n", P528ERR__VALIDATION_D);
        return P528ERR__VALIDATION_D;
    }

    // Validate ranges and logic
    if (params->f__mhz < 100 || params->f__mhz > 30000) {
        printf("P528Error %i: Frequency must be between 100 and 30000 MHz\n", P528ERR__VALIDATION_F);
        return P528ERR__VALIDATION_F;
    }

    if (params->p < 1 || params->p > 99) {
        printf("P528Error %i: Time percentage must be between 1 and 99\n", P528ERR__VALIDATION_P);
        return P528ERR__VALIDATION_P;
    }

    if (params->T_pol != 0 && params->T_pol != 1) {
        printf("P528Error %i: Polarization must be 0 (horizontal) or 1 (vertical)\n", P528ERR__VALIDATION_P);
        return P528ERR__VALIDATION_P;
    }

    if (params->h_1__meter < 1.5 || params->h_1__meter > 80000) {
        printf("P528Error %i: Low terminal height must be between 1.5 and 80000 meters\n", P528ERR__VALIDATION_H1);
        return P528ERR__VALIDATION_H1;
    }

    if (params->h2_start__meter < 1.5 || params->h2_start__meter > 80000) {
        printf("P528Error %i: Starting high terminal height must be between 1.5 and 80000 meters\n", P528ERR__VALIDATION_H2);
        return P528ERR__VALIDATION_H2;
    }

    if (params->h2_end__meter < 1.5 || params->h2_end__meter > 80000) {
        printf("P528Error %i: Ending high terminal height must be between 1.5 and 80000 meters\n", P528ERR__VALIDATION_H2);
        return P528ERR__VALIDATION_H2;
    }

    if (params->h2_end__meter <= params->h2_start__meter) {
        printf("P528Error %i: Ending height must be greater than starting height for ascending trajectory\n", P528ERR__INVALID_HEIGHT_RANGE);
        return P528ERR__INVALID_HEIGHT_RANGE;
    }

    if (params->ascent_rate__mps <= 0) {
        printf("P528Error %i: Ascent rate must be positive\n", P528ERR__INVALID_ASCENT_RATE);
        return P528ERR__INVALID_ASCENT_RATE;
    }

    if (params->start_dist__km < 0 || params->end_dist__km < 0) {
        printf("P528Error %i: Distances cannot be negative\n", P528ERR__VALIDATION_D);
        return P528ERR__VALIDATION_D;
    }

    if (params->end_dist__km < params->start_dist__km) {
        printf("P528Error %i: End distance must be greater than or equal to start distance\n", P528ERR__INVALID_DISTANCE_RANGE);
        return P528ERR__INVALID_DISTANCE_RANGE;
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
    printf("P528Error %i: Option %s is required but was not provided\n", err, opt);
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
    for (int i = 0; i < strlen(argv); i++)
        argv[i] = tolower(argv[i]);
}

/*=============================================================================
 |
 |  Description:  Compare to strings to see if they match
 |
 |        Input:  opt       - Given char array
 |                arg       - Expected char array
 |
 |      Returns:  True/False
 |
 *===========================================================================*/
bool Match(const char* opt, char* arg) {
    if (strcmp(opt, arg) == 0)
        return true;
    return false;
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
    printf("Usage: p528-area [Options]\n");
    printf("Options (not case sensitive)\n");
    printf("\t-h       :: Displays help\n");
    printf("\t-v       :: Displays version information\n");
    printf("\t-h1      :: Height of low terminal, in meters\n");
    printf("\t-h2start :: Starting height of high terminal, in meters (for curve mode)\n");
    printf("\t-h2end   :: Ending height of high terminal, in meters (for curve mode)\n");
    printf("\t-ascent  :: Ascent rate of high terminal, in m/s (for curve mode)\n");
    printf("\t-startdist :: Starting distance of high terminal, in km (for curve mode)\n");
    printf("\t-enddist   :: Ending distance of high terminal, in km (for curve mode)\n");
    printf("\t-heightint :: Height interval between trajectory points, in meters\n");
    printf("\t-f       :: Frequency, in MHz\n");
    printf("\t-p       :: Percentage\n");
    printf("\t-tpol    :: Polarization (0=horizontal, 1=vertical)\n");
    printf("\t-o       :: Output file name\n");
    printf("\n");
    printf("Example:\n");
    printf("\tp528-area -h1 15 -h2start 1000 -h2end 15000 -ascent 10 -startdist 5 -enddist 100 -heightint 500 -f 450 -p 10 -tpol 0 -o trajectory.csv\n");
    printf("\n");
};
