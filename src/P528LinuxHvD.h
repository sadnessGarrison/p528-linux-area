#pragma once

typedef int(*p528func)(double d__km, double h_1__meter, double h_2__meter,
    double f__mhz, int T_pol, double p, struct Result* result);

#include <vector>

//
// CONSTANTS
///////////////////////////////////////////////

#define     MODE_VERSION                            3
#define     TIME_SIZE                               26
#define     MAX_FILENAME_LENGTH                     256

//
// GENERAL ERRORS AND RETURN VALUES
///////////////////////////////////////////////

#define     NOT_SET                                 -1
#define     SUCCESS                                 0
#define     SUCCESS_WITH_WARNINGS                   11
#define     P528__RETURN_SUCCESS                    1000

#define     HVDERR__UNKNOWN                         2001
#define     HVDERR__LIBRARY_LOADING                 2002
#define     HVDERR__INVALID_OPTION                  2003
#define     HVDERR__GETP528_FUNC_LOADING            2004
// Parsing Errors (2010-2099)
#define     HVDERR__PARSE_H1_HEIGHT                 2010
#define     HVDERR__PARSE_F_FREQUENCY               2011
#define     HVDERR__PARSE_P_PERCENTAGE              2012
#define     HVDERR__PARSE_D_DISTANCE                2013
#define     HVDERR__PARSE_TPOL_POLARIZATION         2014
#define     HVDERR__PARSE_TARGET_LOSS               2015
// Validation Errors (2100-2199)
#define     HVDERR__VALIDATION_F                    2101
#define     HVDERR__VALIDATION_P                    2102
#define     HVDERR__VALIDATION_D                    2103
#define     HVDERR__VALIDATION_H1                   2104
#define     HVDERR__VALIDATION_OUT_FILE             2105
#define     HVDERR__VALIDATION_TARGET_LOSS          2106
#define     HVDERR__INVALID_DISTANCE_RANGE          2107

//
// WARNINGS
///////////////////////////////////////////////

#define     HVDWARN__TARGET_NOT_ACHIEVABLE          0x10  // h2 > 20 000 m required

//
// DATA STRUCTURES
///////////////////////////////////////////////

struct HvDData {
    std::vector<double> distances;
    std::vector<double> h2_equivs__meter;  // NAN if target not achievable
    std::vector<double> A__dbs;
    std::vector<double> A_fs__dbs;
    std::vector<double> A_a__dbs;
    std::vector<int> propagation_modes;
    std::vector<int> warnings;

    HvDData(size_t size) {
        distances.reserve(size);
        h2_equivs__meter.reserve(size);
        A__dbs.reserve(size);
        A_fs__dbs.reserve(size);
        A_a__dbs.reserve(size);
        propagation_modes.reserve(size);
        warnings.reserve(size);
    }
};

struct HvDParams {
    double h_1__meter = NOT_SET;    // Low terminal height (m), 1.5 <= h_1 <= 20 000
    double f__mhz = NOT_SET;        // Frequency (MHz), 100 <= f <= 30 000
    double p = NOT_SET;             // Time percentage, 1 <= p <= 99
    int T_pol = NOT_SET;            // Polarization (0=horizontal, 1=vertical)
    double target_A__db = NOT_SET;  // Target basic transmission loss (dB)
    double start_dist__km = NOT_SET;
    double end_dist__km = NOT_SET;
    double distint__km = NOT_SET;

    int mode = 0;

    char out_file[MAX_FILENAME_LENGTH] = { 0 };
};

//
// FUNCTIONS
///////////////////////////////////////////////

int ParseArguments(int argc, char** argv, HvDParams* params);
int ValidateInputs(HvDParams* params);
int LoadLibrary();
int RunHvD(HvDParams* params);
double FindEquivalentHeight(double d__km, double h_1__meter, double f__mhz,
                            int T_pol, double p, double target_A__db,
                            double* achieved_A__db, double* achieved_A_fs__db,
                            double* achieved_A_a__db, int* prop_mode, int* warns);
int WriteResultsToFile(HvDParams* params, const HvDData& data);
int ParseErrorMsgHelper(const char* opt, int err);
int Validate_RequiredErrMsgHelper(const char* opt, int err);
void Lowercase(char* argv);
bool Match(const char* opt, char* arg);
void Help();
void PrintProgress(int current, int total);
