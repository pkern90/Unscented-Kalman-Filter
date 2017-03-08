#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include "lib/Eigen/Dense"
#include "tools.hpp"
#include "ground_truth_package.hpp"
#include "measurement_package.hpp"
#include "lib/cxxopts.hpp"
#include "ukf.hpp"

using namespace std;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using std::vector;

bool verbose = false;
bool useRadar = false;
bool useLidar = false;
string in_file_name_ = "";
string out_file_name_ = "";

void parseOptions(int argc, char *argv[]){
    try {
        cxxopts::Options options(argv[0], " - Implementation of an Unscented Kalman Filter to"
                " fuse lidar and radar sensor data.\n"
                "Input and Output files are required");

        options.add_options()
                ("h,help", "Print help")
                ("i,input", "Input File", cxxopts::value<std::string>())
                ("o,output", "Output file", cxxopts::value<std::string>())
                ("v,verbose", "verbose flag", cxxopts::value<bool>(verbose))
                ("r,radar", "use only radar data", cxxopts::value<bool>(useRadar))
                ("l,lidar", "use only lidar data", cxxopts::value<bool>(useLidar));

        vector<string> optionals = {"input", "output"};
        options.parse_positional(optionals);

        options.parse(argc, argv);

        if (options.count("help")) {
            cout << options.help({"", "Group"}) << endl;
            exit(EXIT_SUCCESS);
        }

        if (options.count("input") == 0) {
            cout << "Please include an input file.\nUse -h to get more information" << std::endl;
            exit(EXIT_FAILURE);
        }

        if (options.count("output") == 0) {
            cout << "Please include an output file.\nUse -h to get more information" << std::endl;
            exit(EXIT_FAILURE);
        }

        in_file_name_ = options["input"].as<string>();
        out_file_name_ = options["output"].as<string>();

    } catch (const cxxopts::OptionException &e) {
        std::cout << "error parsing options: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }
}

void check_files(ifstream &in_file, string &in_name,
                 ofstream &out_file, string &out_name) {
    if (!in_file.is_open()) {
        cerr << "Cannot open input file: " << in_name << endl;
        exit(EXIT_FAILURE);
    }

    if (!out_file.is_open()) {
        cerr << "Cannot open output file: " << out_name << endl;
        exit(EXIT_FAILURE);
    }
}

void readData(ifstream &in_file_, vector<MeasurementPackage> &measurement_pack_list,
              vector<GroundTruthPackage> &gt_pack_list) {
    string line;

    // prep the measurement packages (each line represents a measurement at a timestamp)
    while (getline(in_file_, line)) {

        string sensor_type;
        MeasurementPackage meas_package;
        GroundTruthPackage gt_package;
        istringstream iss(line);
        long timestamp;

        // reads first element from the current line
        iss >> sensor_type;
        if (sensor_type.compare("L") == 0) {
            // LASER MEASUREMENT
            if(useRadar){
                continue;
            }

            // read measurements at this timestamp
            meas_package.sensor_type_ = MeasurementPackage::LASER;
            meas_package.raw_measurements_ = VectorXd(2);
            float x;
            float y;
            iss >> x;
            iss >> y;
            meas_package.raw_measurements_ << x, y;
            iss >> timestamp;
            meas_package.timestamp_ = timestamp;
            measurement_pack_list.push_back(meas_package);
        } else if (sensor_type.compare("R") == 0) {
            // RADAR MEASUREMENT
            if(useLidar){
                continue;
            }

            // read measurements at this timestamp
            meas_package.sensor_type_ = MeasurementPackage::RADAR;
            meas_package.raw_measurements_ = VectorXd(3);
            float ro;
            float theta;
            float ro_dot;
            iss >> ro;
            iss >> theta;
            iss >> ro_dot;
            meas_package.raw_measurements_ << ro, theta, ro_dot;
            iss >> timestamp;
            meas_package.timestamp_ = timestamp;
            measurement_pack_list.push_back(meas_package);
        }

        // read ground truth data to compare later
        float x_gt;
        float y_gt;
        float vx_gt;
        float vy_gt;
        iss >> x_gt;
        iss >> y_gt;
        iss >> vx_gt;
        iss >> vy_gt;
        gt_package.gt_values_ = VectorXd(4);
        gt_package.gt_values_ << x_gt, y_gt, vx_gt, vy_gt;
        gt_pack_list.push_back(gt_package);
    }
}

void processData(ofstream &out_file_, const vector<MeasurementPackage> &measurement_pack_list,
                 const vector<GroundTruthPackage> &gt_pack_list, vector<VectorXd> &estimations,
                 vector<VectorXd> &ground_truth) {

    UKF ukf;

    //Call the EKF-based fusion
    size_t N = measurement_pack_list.size();
    for (size_t k = 0; k < N; ++k) {
        // Call the UKF-based fusion
        ukf.ProcessMeasurement(measurement_pack_list[k]);

        // output the estimation
        out_file_ << ukf.x_(0) << "\t"; // pos1 - est
        out_file_ << ukf.x_(1) << "\t"; // pos2 - est
        out_file_ << ukf.x_(2) << "\t"; // vel_abs -est
        out_file_ << ukf.x_(3) << "\t"; // yaw_angle -est
        out_file_ << ukf.x_(4) << "\t"; // yaw_rate -est

        // output the measurements
        if (measurement_pack_list[k].sensor_type_ == MeasurementPackage::LASER) {
            // output the estimation
            out_file_ << measurement_pack_list[k].raw_measurements_(0) << "\t";
            out_file_ << measurement_pack_list[k].raw_measurements_(1) << "\t";
        } else if (measurement_pack_list[k].sensor_type_ == MeasurementPackage::RADAR) {
            // output the estimation in the cartesian coordinates
            double ro = measurement_pack_list[k].raw_measurements_(0);
            double phi = measurement_pack_list[k].raw_measurements_(1);
            out_file_ << ro * cos(phi) << "\t"; // p1_meas
            out_file_ << ro * sin(phi) << "\t"; // p2_meas
        }

        estimations.push_back(ukf.x_.head(2));
        ground_truth.push_back(gt_pack_list[k].gt_values_.head(2));

        if(verbose){
            cout << "***** Entry: " << (k + 1) << " *****\n" << endl;
            cout << "x_ = " << ukf.x_ << "\n" << endl;
            cout << "P_ = " << ukf.P_ << "\n" << endl;
        }
    }
}

int main(int argc, char *argv[]) {
    parseOptions(argc, argv);

    ifstream in_file_(in_file_name_.c_str(), ifstream::in);
    ofstream out_file_(out_file_name_.c_str(), ofstream::out);

    check_files(in_file_, in_file_name_, out_file_, out_file_name_);

    vector<MeasurementPackage> measurement_pack_list;
    vector<GroundTruthPackage> gt_pack_list;
    readData(in_file_, measurement_pack_list, gt_pack_list);

    // used to compute the RMSE later
    vector<VectorXd> estimations;
    vector<VectorXd> ground_truth;
    processData(out_file_, measurement_pack_list, gt_pack_list, estimations, ground_truth);

    // compute the accuracy (RMSE)
    cout << "Accuracy - RMSE:" << endl << tools::CalculateRMSE(estimations, ground_truth) << endl;

    // close files
    if (out_file_.is_open()) {
        out_file_.close();
    }

    if (in_file_.is_open()) {
        in_file_.close();
    }

    return EXIT_SUCCESS;
}