#include <iostream>
#include "ukf.hpp"

using Eigen::VectorXd;
using Eigen::MatrixXd;

/**
 * Initializes Unscented Kalman filter
 */
UKF::UKF() {
    /*****************************************************************************
     *  Process noise
     ****************************************************************************/

    // Process noise standard deviation longitudinal acceleration in m/s^2
    std_a_ = .63;

    // Process noise standard deviation yaw acceleration in rad/s^2
    std_yawdd_ = 1.2;

    /*****************************************************************************
     *  Laser measurement noise
     ****************************************************************************/

    // Laser measurement noise standard deviation position1 in m
    std_laspx_ = 0.0225;

    // Laser measurement noise standard deviation position2 in m
    std_laspy_ = 0.0225;

    /*****************************************************************************
     *  Radar measurement noise
     ****************************************************************************/

    // Radar measurement noise standard deviation radius in m
    std_radr_ = 0.9;

    // Radar measurement noise standard deviation angle in rad
    std_radphi_ = 0.005;

    // Radar measurement noise standard deviation radius change in m/s
    std_radrd_ = 0.5;

    /*****************************************************************************
     *  Initialisation
     ****************************************************************************/

    is_initialized_ = false;

    n_x_ = 5;

    n_aug_ = n_x_ + 2;

    lambda_ = 3 - n_aug_;

    // initial state vector
    x_ = VectorXd(5);
    x_aug = VectorXd(7);

    Q_ = MatrixXd(2, 2);
    Q_ << std_a_ * std_a_, 0,
            0, std_yawdd_ * std_yawdd_;

    // initial covariance matrix
    P_ = MatrixXd(5, 5);
    P_ << 1, 0, 0, 0, 0,
            0, 1, 0, 0, 0,
            0, 0, 1000, 0, 0,
            0, 0, 0, 100, 0,
            0, 0, 0, 0, 1;

    P_aug = MatrixXd(7, 7);
    P_aug.topLeftCorner(n_x_, n_x_) = P_;
    P_aug.bottomRightCorner(Q_.rows(), Q_.cols()) = Q_;

    weights_ = VectorXd(2 * n_aug_ + 1);
    weights_.segment(1, 2 * n_aug_).fill(0.5d / (n_aug_ + lambda_));
    weights_(0) = lambda_ / (lambda_ + n_aug_);

    //create sigma point matrix
    Xsig_ = MatrixXd(n_aug_, 2 * n_aug_ + 1);
    Xsig_pred_ = MatrixXd(n_x_, 2 * n_aug_ + 1);

    n_z_radar_ = 3;
    Zsig_ = MatrixXd(n_z_radar_, 2 * n_aug_ + 1);


    H_laser_ = MatrixXd(2, 5);
    H_laser_ << 1, 0, 0, 0, 0,
            0, 1, 0, 0, 0;

    R_laser_ = MatrixXd(2, 2);
    R_laser_ << std_laspx_, 0,
            0, std_laspy_;

    R_radar_ = MatrixXd(n_z_radar_, n_z_radar_);
    R_radar_ << std_radr_ * std_radr_, 0, 0,
            0, std_radphi_ * std_radphi_, 0,
            0, 0, std_radrd_ * std_radrd_;
}

UKF::~UKF() {}

/**
 * @param {MeasurementPackage} meas_package The latest measurement data of
 * either radar or laser.
 */
void UKF::ProcessMeasurement(MeasurementPackage measurement_pack) {
    /*****************************************************************************
     *  Initialization
     ****************************************************************************/
    if (!is_initialized_) {
        // first measurement
        std::cout << "UKF: " << std::endl;

        double px = 0;
        double py = 0;

        if (measurement_pack.sensor_type_ == MeasurementPackage::RADAR) {
            double rho = measurement_pack.raw_measurements_[0];
            double phi = measurement_pack.raw_measurements_[1];

            px = rho * cos(phi);
            py = rho * sin(phi);

            // If initial values are zero they will set to an initial guess
            // and the uncertainty will be increased.
            // Initial zeros would cause the algorithm to fail when using only Radar data.
            if(fabs(px) < 0.0001){
                px = 1;
                P_(0,0) = 1000;
            }
            if(fabs(py) < 0.0001){
                py = 1;
                P_(1,1) = 1000;
            }

        } else if (measurement_pack.sensor_type_ == MeasurementPackage::LASER) {
            px = measurement_pack.raw_measurements_[0];
            py = measurement_pack.raw_measurements_[1];
        }

        x_ << px, py, 0, 0, 0;
        x_aug << x_.array(), 0, 0;
        previous_timestamp_ = measurement_pack.timestamp_;

        is_initialized_ = true;
        return;
    }

    /*****************************************************************************
     *  Prediction
     ****************************************************************************/

    double dt = (measurement_pack.timestamp_ - previous_timestamp_) / 1000000.0;
    previous_timestamp_ = measurement_pack.timestamp_;

    Prediction(dt);

    /*****************************************************************************
     *  Update
     ****************************************************************************/


    if (measurement_pack.sensor_type_ == MeasurementPackage::RADAR) {
        // Radar updates
        UpdateRadar(measurement_pack);
    } else {
        // Laser updates
        UpdateLidar(measurement_pack);
    }
}

/**
 * Predicts sigma points, the state, and the state covariance matrix.
 * @param {double} delta_t the change in time (in seconds) between the last
 * measurement and this one.
 */
void UKF::Prediction(double delta_t) {

    //create augmented mean state
    x_aug << x_.array(), 0, 0;

    P_aug.topLeftCorner(n_x_, n_x_) = P_;
    P_aug.bottomRightCorner(Q_.rows(), Q_.cols()) = Q_;

    //calculate square root of P
    MatrixXd A = P_aug.llt().matrixL();

    //create augmented sigma points
    Xsig_.colwise() = x_aug;
    MatrixXd offset = A * sqrt(lambda_ + n_aug_);

    Xsig_.block(0, 1, n_aug_, n_aug_) += offset;
    Xsig_.block(0, n_aug_ + 1, n_aug_, n_aug_) -= offset;


    //predict sigma points
    for (int i = 0; i < 2 * n_aug_ + 1; i++) {
        //extract values for better readability
        double p_x = Xsig_(0, i);
        double p_y = Xsig_(1, i);
        double v = Xsig_(2, i);
        double yaw = Xsig_(3, i);
        double yawd = Xsig_(4, i);
        double nu_a = Xsig_(5, i);
        double nu_yawdd = Xsig_(6, i);

        //predicted state values
        double px_p, py_p;

        //avoid division by zero
        if (fabs(yawd) > 0.001) {
            px_p = p_x + v / yawd * (sin(yaw + yawd * delta_t) - sin(yaw));
            py_p = p_y + v / yawd * (cos(yaw) - cos(yaw + yawd * delta_t));
        } else {
            px_p = p_x + v * delta_t * cos(yaw);
            py_p = p_y + v * delta_t * sin(yaw);
        }

        double v_p = v;
        double yaw_p = yaw + yawd * delta_t;
        double yawd_p = yawd;

        //add noise
        px_p = px_p + 0.5 * nu_a * delta_t * delta_t * cos(yaw);
        py_p = py_p + 0.5 * nu_a * delta_t * delta_t * sin(yaw);
        v_p = v_p + nu_a * delta_t;

        yaw_p = yaw_p + 0.5 * nu_yawdd * delta_t * delta_t;
        yawd_p = yawd_p + nu_yawdd * delta_t;

        //write predicted sigma point into right column
        Xsig_pred_(0, i) = px_p;
        Xsig_pred_(1, i) = py_p;
        Xsig_pred_(2, i) = v_p;
        Xsig_pred_(3, i) = yaw_p;
        Xsig_pred_(4, i) = yawd_p;
    }

    //predicted state mean
    x_.fill(0.0);
    for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //iterate over sigma points
        x_ = x_ + weights_(i) * Xsig_pred_.col(i);
    }

    //predicted state covariance matrix
    P_.fill(0.0);
    for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //iterate over sigma points

        // state difference
        VectorXd x_diff = Xsig_pred_.col(i) - x_;
        //angle normalization
        while (x_diff(3) > M_PI) x_diff(3) -= 2. * M_PI;
        while (x_diff(3) < -M_PI) x_diff(3) += 2. * M_PI;

        P_ = P_ + weights_(i) * x_diff * x_diff.transpose();
    }
}

/**
 * Updates the state and the state covariance matrix using a laser measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateLidar(MeasurementPackage measurement_pack) {
    VectorXd z = measurement_pack.raw_measurements_;

    VectorXd z_pred = H_laser_ * x_;
    VectorXd z_diff = z - z_pred;
    MatrixXd Ht = H_laser_.transpose();
    MatrixXd S = H_laser_ * P_ * Ht + R_laser_;
    MatrixXd Si = S.inverse();
    MatrixXd PHt = P_ * Ht;
    MatrixXd K = PHt * Si;

    //new estimate
    x_ = x_ + (K * z_diff);
    long x_size = x_.size();
    MatrixXd I = MatrixXd::Identity(x_size, x_size);
    P_ = (I - K * H_laser_) * P_;

    NIS_laser_ = z_diff.transpose() * Si * z_diff;
}

/**
 * Updates the state and the state covariance matrix using a radar measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateRadar(MeasurementPackage measurement_pack) {
    //transform sigma points into measurement space
    for (int i = 0; i < 2 * n_aug_ + 1; i++) {

        // extract values for better readibility
        double p_x = Xsig_pred_(0, i);
        double p_y = Xsig_pred_(1, i);
        double v = Xsig_pred_(2, i);
        double yaw = Xsig_pred_(3, i);

        double v_y = cos(yaw) * v;
        double v_x = sin(yaw) * v;

        // measurement model
        double rho = sqrt(p_x * p_x + p_y * p_y);
        double phi = atan2(p_y, p_x);
        double rho_dot = (p_x * v_y + p_y * v_x) / rho;

        if (rho != rho) {
            rho = 0;
        }
        if (phi != phi) {
            phi = 0;
        }
        if (rho_dot != rho_dot) {
            rho_dot = 0;
        }

        Zsig_(0, i) = rho;
        Zsig_(1, i) = phi;
        Zsig_(2, i) = rho_dot;
    }

    //mean predicted measurement
    VectorXd z_pred = VectorXd(n_z_radar_);
    z_pred.fill(0.0);
    for (int i = 0; i < 2 * n_aug_ + 1; i++) {
        z_pred = z_pred + weights_(i) * Zsig_.col(i);
    }

    //measurement covariance matrix S
    MatrixXd S = MatrixXd(n_z_radar_, n_z_radar_);
    S.fill(0.0);
    for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //2n+1 simga points
        //residual
        VectorXd z_diff = Zsig_.col(i) - z_pred;

        //angle normalization
        while (z_diff(1) > M_PI) z_diff(1) -= 2. * M_PI;
        while (z_diff(1) < -M_PI) z_diff(1) += 2. * M_PI;

        S = S + weights_(i) * z_diff * z_diff.transpose();
    }

    //add measurement noise covariance matrix
    S = S + R_radar_;

    //calculate cross correlation matrix
    MatrixXd Tc = MatrixXd(n_x_, n_z_radar_);
    Tc.fill(0.0);
    for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //2n+1 simga points

        //residual
        VectorXd z_diff = Zsig_.col(i) - z_pred;
        //angle normalization
        while (z_diff(1) > M_PI) z_diff(1) -= 2. * M_PI;
        while (z_diff(1) < -M_PI) z_diff(1) += 2. * M_PI;

        // state difference
        VectorXd x_diff = Xsig_pred_.col(i) - x_;
        //angle normalization
        while (x_diff(3) > M_PI) x_diff(3) -= 2. * M_PI;
        while (x_diff(3) < -M_PI) x_diff(3) += 2. * M_PI;

        Tc = Tc + weights_(i) * x_diff * z_diff.transpose();
    }

    //Kalman gain K;
    MatrixXd K = Tc * S.inverse();

    //residual
    VectorXd z = measurement_pack.raw_measurements_;
    VectorXd z_diff = z - z_pred;

    //angle normalization
    while (z_diff(1) > M_PI) z_diff(1) -= 2. * M_PI;
    while (z_diff(1) < -M_PI) z_diff(1) += 2. * M_PI;

    //update state mean and covariance matrix
    x_ = x_ + K * z_diff;
    P_ = P_ - K * S * K.transpose();

    NIS_radar_ = z_diff.transpose() * S.inverse() * z_diff;
}
