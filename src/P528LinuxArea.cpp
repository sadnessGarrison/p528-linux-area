#include <dlfcn.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include "P528LinuxArea.h"

/*=============================================================================
 |
 |  Description:  This driver allows the user to execute the P.528 model
 |                DLL and generate results.  For full details and examples
 |                on how to use it, please see the readme.txt file.
 |
 *===========================================================================*/

// Local globals
void* hLib = nullptr;
p528func dllP528 = nullptr;

int dllVerMajor = 0;
int dllVerMinor = 0;
int drvrVerMajor = 0;
int drvrVerMinor = 0;

char buf[TIME_SIZE];

/*=============================================================================
 |
 |  Description:  Main function of the P.528 driver executable
 |
 *===========================================================================*/
int main(int argc, char** argv) {
    int rtn;
    DrvrParams params;

    // Get the time
    time_t t = time(NULL);
    if (ctime_r(&t, buf) == NULL) {
        buf[0] = '\0';
    }

    rtn = ParseArguments(argc, argv, &params);
    if (rtn == DRVR__RETURN_SUCCESS)
        return SUCCESS;
    if (rtn) {
        Help();
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

    rtn = LoadDLL();
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
 |      Returns:  P.528 DLL return code
 |
 *===========================================================================*/
int CallP528_CURVE(DrvrParams* params) {
    Result result;
    int rtn = SUCCESS;

    double* A__dbs = (double*)malloc(sizeof(double) * CURVE_POINTS);
    double* A_fs__dbs = (double*)malloc(sizeof(double) * CURVE_POINTS);
    int* warnings = (int*)malloc(sizeof(int) * CURVE_POINTS);
    if (!A__dbs || !A_fs__dbs || !warnings) {
        free(A__dbs); free(A_fs__dbs); free(warnings);
        return DRVRERR__UNKNOWN;
    }

    // Gather data points
    for (int d__km = 0; d__km < CURVE_POINTS; d__km++) {
        rtn = dllP528(d__km, params->h_1__meter, params->h_2__meter, params->f__mhz, params->T_pol, params->p, &result);

        A__dbs[d__km] = result.A__db;
        A_fs__dbs[d__km] = result.A_fs__db;
        warnings[d__km] = result.warnings;

        if (rtn != SUCCESS && rtn != SUCCESS_WITH_WARNINGS)
            break;
    }

    // Print results to file
    FILE* fp = fopen(params->out_file, "w");
    if (!fp) {
        free(A__dbs); free(A_fs__dbs); free(warnings);
        printf("Error opening output file (%s). Exiting.\n", params->out_file);
        return DRVRERR__VALIDATION_OUT_FILE;
    }

    // fprintf(fp, "p528 shared object Version,%i.%i\n", dllVerMajor, dllVerMinor);
    // fprintf(fp, "P528Drvr Version,%i.%i\n", drvrVerMajor, drvrVerMinor);
    fprintf(fp, "Date Generated,%s", buf);
    fprintf(fp, "\n");
    fprintf(fp, "Inputs\n");
    fprintf(fp, "h_1__meter,%f\n", params->h_1__meter);
    fprintf(fp, "h_2__meter,%f\n", params->h_2__meter);
    fprintf(fp, "f__mhz,%f\n", params->f__mhz);
    fprintf(fp, "p,%f\n", params->p);
    fprintf(fp, "T_pol,%i\n", params->T_pol);
    fprintf(fp, "\n");

    if (rtn != SUCCESS && rtn != SUCCESS_WITH_WARNINGS) {
        fprintf(fp, "P.528 returned error,%i\n", rtn);
    }
    else {
        fprintf(fp, "Results\n");

        fprintf(fp, "Distance (km)");
        for (int d__km = 0; d__km < CURVE_POINTS; d__km++)
            fprintf(fp, ",%i", d__km);
        fprintf(fp, "\n");

        fprintf(fp, "Free Space Loss (dB),%.3f", A_fs__dbs[0]);
        for (int i = 1; i < CURVE_POINTS; i++)
            fprintf(fp, ",%.3f", A_fs__dbs[i]);
        fprintf(fp, "\n");

        fprintf(fp, "Basic Transmission Loss (dB),%.3f", A__dbs[0]);
        for (int i = 1; i < CURVE_POINTS; i++)
            fprintf(fp, ",%.3f", A__dbs[i]);
        fprintf(fp, "\n");

        fprintf(fp, "Warnings,0x%x", warnings[0]);
        for (int i = 1; i < CURVE_POINTS; i++)
            fprintf(fp, ",0x%x", warnings[i]);
        fprintf(fp, "\n");
    }

    fclose(fp);
    free(A__dbs); free(A_fs__dbs); free(warnings);

    return rtn;
}

/*=============================================================================
 |
 |  Description:  Loads the P.528 DLL
 |
 |        Input:  [void]
 |
 |      Returns:  [void]
 |
 *===========================================================================*/
int LoadDLL() {
    // Try common shared object names
    const char* candidates[] = {"libp528.so", "p528.so", "./libp528.so", NULL};
    const char** c = candidates;
    while (*c) {
        hLib = dlopen(*c, RTLD_NOW);
        if (hLib)
            break;
        c++;
    }

    if (!hLib) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return DRVRERR__DLL_LOADING;
    }

    // Resolve the symbol P528 (must be exported with C linkage)
    dlerror();
    dllP528 = (p528func)dlsym(hLib, "P528");
    char* err = dlerror();
    if (err != NULL || dllP528 == nullptr) {
        fprintf(stderr, "dlsym(P528) failed: %s\n", err ? err : "unknown");
        dlclose(hLib);
        hLib = nullptr;
        return DRVRERR__GETP528_FUNC_LOADING;
    }

    return SUCCESS;
}

/*=============================================================================
 |
 |  Description:  Get the version information of the P.528 DLL
 |
 |        Input:  [void]
 |
 |      Returns:  [void]
 |
 *===========================================================================*/
void GetDLLVersionInfo() {
    // Not implemented on Linux; shared objects typically don't carry the same version resource.
    dllVerMajor = 0;
    dllVerMinor = 0;
}

/*=============================================================================
 |
 |  Description:  Get the version information of this driver
 |
 |        Input:  [void]
 |
 |      Returns:  [void]
 |
 *===========================================================================*/
void GetDrvrVersionInfo()
{
    // Not implemented on Linux; keep driver version at 0.0 unless you want to embed version strings.
    drvrVerMajor = 0;
    drvrVerMinor = 0;
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
    printf("DrvrErr %i: Unable to parse %s value.\n", err, opt);
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
int ParseArguments(int argc, char** argv, DrvrParams* params)
{
    for (int i = 1; i < argc; i++) {
        Lowercase(argv[i]);

        if (Match("-h1", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->h_1__meter)) != 1)
                return ParseErrorMsgHelper("-h1 [height]", DRVRERR__PARSE_H1_HEIGHT);
            i++;
        }
        else if (Match("-h2", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->h_2__meter)) != 1)
                return ParseErrorMsgHelper("-h2 [height]", DRVRERR__PARSE_H2_HEIGHT);
            i++;
        }
        else if (Match("-f", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->f__mhz)) != 1)
                return ParseErrorMsgHelper("-f [frequency]", DRVRERR__PARSE_F_FREQUENCY);
            i++;
        }
        else if (Match("-d", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->d__km)) != 1)
                return ParseErrorMsgHelper("-d [distance]", DRVRERR__PARSE_D_DISTANCE);
            i++;
        }
        else if (Match("-p", argv[i])) {
            if (sscanf(argv[i + 1], "%lf", &(params->p)) != 1)
                return ParseErrorMsgHelper("-p [percentage]", DRVRERR__PARSE_P_PERCENTAGE);
            i++;
        }
        else if (Match("-tpol", argv[i])) {
            if (sscanf(argv[i + 1], "%i", &(params->T_pol)) != 1)
                return ParseErrorMsgHelper("-tpol [polarization]", DRVRERR__PARSE_TPOL_POLARIZATION);
            i++;
        }
        else if (Match("-o", argv[i])) {
            strncpy(params->out_file, argv[i + 1], sizeof(params->out_file)-1);
            params->out_file[sizeof(params->out_file)-1] = '\0';
            i++;
        }
        // else if (Match("-mode", argv[i]))
        // {
        //     Lowercase(argv[i + 1]);

        //     if (Match("point", argv[i + 1]))
        //         params->mode = MODE_POINT;
        //     else if (Match("curve", argv[i + 1]))
        //         params->mode = MODE_CURVE;
        //     else if (Match("table", argv[i + 1]))
        //         params->mode = MODE_TABLE;
        //     else
        //         return ParseErrorMsgHelper("-mode [mode]", DRVRERR__PARSE_MODE_VALUE);

        //     i++;
        // }
        else if (Match("-v", argv[i])) {
            params->mode = MODE_VERSION;
            return SUCCESS;
        }
        else if (Match("-h", argv[i])) {
            Help();
            return DRVR__RETURN_SUCCESS;
        }
        else {
            printf("Unknown option: %s\n", argv[i]);
            return DRVRERR__INVALID_OPTION;
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
int ValidateInputs(DrvrParams* params) {
    if (params->f__mhz == NOT_SET)
        return Validate_RequiredErrMsgHelper("-f", DRVRERR__VALIDATION_F);

    if (params->p == NOT_SET)
        return Validate_RequiredErrMsgHelper("-p", DRVRERR__VALIDATION_P);

    if (params->T_pol == NOT_SET)
        return Validate_RequiredErrMsgHelper("-tpol", DRVRERR__VALIDATION_P);

    // if (params->mode == NOT_SET)
    //     return Validate_RequiredErrMsgHelper("-mode", DRVRERR__VALIDATION_MODE);

    if (params->mode == MODE_POINT) {
        if (params->d__km == NOT_SET)
            return Validate_RequiredErrMsgHelper("-d", DRVRERR__VALIDATION_D);
    }

    if (params->mode == MODE_POINT || params->mode == MODE_CURVE) {
        if (params->h_1__meter == NOT_SET)
            return Validate_RequiredErrMsgHelper("-h1", DRVRERR__VALIDATION_H1);

        if (params->h_2__meter == NOT_SET)
            return Validate_RequiredErrMsgHelper("-h2", DRVRERR__VALIDATION_H2);
    }

    if (params->mode == MODE_CURVE || params->mode == MODE_TABLE) {
        if (strlen(params->out_file) == 0)
            return  Validate_RequiredErrMsgHelper("-o", DRVRERR__VALIDATION_OUT_FILE);
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
    printf("DrvrError %i: Option %s is required but was not provided\n", err, opt);
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
    printf("\t-h    :: Displays help\n");
    printf("\t-v    :: Displays version information\n");
    printf("\t-h1   :: Height of low terminal, in meters\n");
    printf("\t-h2   :: Height of high terminal, in meters\n");
    printf("\t-f    :: Frequency, in MHz\n");
    printf("\t-p    :: Percentage\n");
    printf("\t-tpol :: Polarization\n");
    printf("\t-d    :: Path distance, in km\n");
    printf("\t-o    :: Output file name\n");
    printf("\n");
    printf("Example:\n");
    printf("\tp528-area -h1 15 -h2 15000 -f 450 -p 10 -tpol 0 -o curve.csv\n");
    printf("\n");
};
