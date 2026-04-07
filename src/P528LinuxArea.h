typedef int(*p528func)(double d__km, double h_1__meter, double h_2__meter, 
    double f__mhz, int T_pol, double p, struct Result* result);

#include <vector>

//
// CONSTANTS
///////////////////////////////////////////////

#define     MODE_VERSION                            3
#define     TIME_SIZE                               26
#define     MAX_FILENAME_LENGTH                      256
#define     METERS_TO_KM                            1000.0

//
// GENERAL ERRORS AND RETURN VALUES
///////////////////////////////////////////////

#define     NOT_SET                                 -1
#define     SUCCESS                                 0
#define     SUCCESS_WITH_WARNINGS                   11
#define     P528__RETURN_SUCCESS                    1000

#define     P528ERR__UNKNOWN                        1001
#define     P528ERR__LIBRARY_LOADING                1002
#define     P528ERR__INVALID_OPTION                 1004
#define     P528ERR__GETP528_FUNC_LOADING           1005
// Parsing Errors (1000-1099)
#define     P528ERR__PARSE_H1_HEIGHT                1010
#define     P528ERR__PARSE_H2_HEIGHT                1011
#define     P528ERR__PARSE_F_FREQUENCY              1012
#define     P528ERR__PARSE_D_DISTANCE               1013
#define     P528ERR__PARSE_P_PERCENTAGE             1014
#define     P528ERR__PARSE_TPOL_POLARIZATION        1016
// Validation Errors (1100-1199)
#define     P528ERR__VALIDATION_F                   1101
#define     P528ERR__VALIDATION_P                   1102
#define     P528ERR__VALIDATION_D                   1103
#define     P528ERR__VALIDATION_H1                  1104
#define     P528ERR__VALIDATION_H2                  1105
#define     P528ERR__VALIDATION_OUT_FILE            1106
#define     P528ERR__MEMORY_ALLOCATION              1107
#define     P528ERR__INVALID_DISTANCE_RANGE         1108
#define     P528ERR__INVALID_HEIGHT_RANGE           1109
#define     P528ERR__INVALID_ASCENT_RATE            1110

//
// WARNINGS
///////////////////////////////////////////////

#define WARNING__NO_WARNINGS                0x00
#define WARNING__DFRAC_TROPO_REGION         0x01
#define WARNING__HEIGHT_LIMIT_H_1           0x02
#define WARNING__HEIGHT_LIMIT_H_2           0x04

//
// DATA STRUCTURES
///////////////////////////////////////////////

// struct Result is defined in p528.h

struct TrajectoryData {
    std::vector<double> times;
    std::vector<double> heights;
    std::vector<double> distances;
    std::vector<double> A__dbs;
    std::vector<double> A_fs__dbs;
    std::vector<double> A_a__dbs;
    std::vector<double> A_excess__dbs;  // A__db - A_fs__db - A_a__db (path correction + variability)
    std::vector<double> r_0__kms;
    std::vector<int> propagation_modes;
    std::vector<int> warnings;
    
    TrajectoryData(size_t size) {
        times.reserve(size);
        heights.reserve(size);
        distances.reserve(size);
        A__dbs.reserve(size);
        A_fs__dbs.reserve(size);
        A_a__dbs.reserve(size);
        A_excess__dbs.reserve(size);
        r_0__kms.reserve(size);
        propagation_modes.reserve(size);
        warnings.reserve(size);
    }
};

struct P528Params {
    double h_1__meter = NOT_SET;  // Low terminal height (meter), 1.5 <= h_1__meter <= 80 000
    double h2_start__meter = NOT_SET;  // Starting height of high terminal (meter), 1.5 <= h2_start__meter <= 80 000
    double h2_end__meter = NOT_SET;    // Ending height of high terminal (meter), 1.5 <= h2_end__meter <= 80 000
    double ascent_rate__mps = NOT_SET; // Ascent rate of high terminal (m/s), must be > 0
    double start_dist__km = NOT_SET;   // Starting distance of high terminal (km), must be >= 0
    double end_dist__km = NOT_SET;     // Ending distance of high terminal (km), must be >= start_dist
    double heightint__meter = NOT_SET; // Height interval (meter), must be > 0
    double f__mhz = NOT_SET;      // Frequency (MHz), 100 <= f__mhz <= 30 000
    double p = NOT_SET;           // Time percentage, 1 <= p <= 99
    int T_pol = NOT_SET;          // Polarization (0=horizontal, 1=vertical)

    int mode = 0;                 // Mode (0 for normal, MODE_VERSION for version)

    char out_file[MAX_FILENAME_LENGTH] = { 0 };   // Output file
};

//
// FUNCTIONS
///////////////////////////////////////////////

int ParseArguments(int argc, char** argv, P528Params* params);
void Lowercase(char* argv);
bool Match(const char* opt, char* arg);
void Help();
int ParseErrorMsgHelper(const char* opt, int err);
int ValidateInputs(P528Params* params);
int Validate_RequiredErrMsgHelper(const char* opt, int err);
int LoadLibrary();
int CallP528_CURVE(P528Params* params);
int CalculateTrajectoryPoints(P528Params* params, TrajectoryData& data);
int ComputePathLosses(P528Params* params, TrajectoryData& data);
int WriteResultsToFile(P528Params* params, const TrajectoryData& data);
void PrintProgress(int current, int total);
