#ifndef lvio_fusion_LOOP_ERROR_H
#define lvio_fusion_LOOP_ERROR_H

#include "lvio_fusion/ceres/base.hpp"
#include "lvio_fusion/ceres/lidar_error.hpp"

namespace lvio_fusion
{

class PoseGraphError
{
public:
    PoseGraphError(SE3d last_frame, SE3d frame, double weight)
        : relative_i_j_(last_frame.inverse() * frame), weight_(weight) {}

    template <typename T>
    bool operator()(const T *Twc1, const T *Twc2, T *residuals) const
    {
        T Twc1_inverse[7], relative_i_j[7];
        ceres::SE3Inverse(Twc1, Twc1_inverse);
        ceres::SE3Product(Twc1_inverse, Twc2, relative_i_j);
        residuals[0] = T(weight_) * (T(relative_i_j_.data()[0]) - relative_i_j[0]);
        residuals[1] = T(weight_) * (T(relative_i_j_.data()[1]) - relative_i_j[1]);
        residuals[2] = T(weight_) * (T(relative_i_j_.data()[2]) - relative_i_j[2]);
        residuals[3] = T(weight_) * (T(relative_i_j_.data()[3]) - relative_i_j[3]);
        residuals[4] = T(weight_) * (T(relative_i_j_.data()[4]) - relative_i_j[4]);
        residuals[5] = T(weight_) * (T(relative_i_j_.data()[5]) - relative_i_j[5]);
        residuals[6] = T(weight_) * (T(relative_i_j_.data()[6]) - relative_i_j[6]);
        return true;
    }

    static ceres::CostFunction *Create(SE3d last_frame, SE3d frame, double weight = 1)
    {
        return (new ceres::AutoDiffCostFunction<PoseGraphError, 7, 7, 7>(new PoseGraphError(last_frame, frame, weight)));
    }

private:
    SE3d relative_i_j_;
    double weight_;
};

class PoseGraphTError
{
public:
    PoseGraphTError(SE3d last_frame, SE3d frame, double weight)
        : relative_i_j_(last_frame.inverse() * frame), weight_(weight) {}

    template <typename T>
    bool operator()(const T *Twc1, const T *Twc2, T *residuals) const
    {
        T Twc1_inverse[7], relative_i_j[7];
        ceres::SE3Inverse(Twc1, Twc1_inverse);
        ceres::SE3Product(Twc1_inverse, Twc2, relative_i_j);
        residuals[0] = T(weight_) * T(relative_i_j_.data()[4]) - relative_i_j[4];
        residuals[1] = T(weight_) * T(relative_i_j_.data()[5]) - relative_i_j[5];
        residuals[2] = T(weight_) * T(relative_i_j_.data()[6]) - relative_i_j[6];
        return true;
    }

    static ceres::CostFunction *Create(SE3d last_frame, SE3d frame, double weight = 1)
    {
        return (new ceres::AutoDiffCostFunction<PoseGraphTError, 3, 7, 7>(new PoseGraphTError(last_frame, frame, weight)));
    }

private:
    SE3d relative_i_j_;
    double weight_;
};

class PoseError
{
public:
    PoseError(SE3d pose, double weight) : pose_(pose), weight_(weight) {}

    template <typename T>
    bool operator()(const T *pose, T *residuals) const
    {
        residuals[0] = T(weight_) * (pose[0] - T(pose_.data()[0]));
        residuals[1] = T(weight_) * (pose[1] - T(pose_.data()[1]));
        residuals[2] = T(weight_) * (pose[2] - T(pose_.data()[2]));
        residuals[3] = T(weight_) * (pose[3] - T(pose_.data()[3]));
        residuals[4] = T(weight_) * (pose[4] - T(pose_.data()[4]));
        residuals[5] = T(weight_) * (pose[5] - T(pose_.data()[5]));
        residuals[6] = T(weight_) * (pose[6] - T(pose_.data()[6]));
        return true;
    }

    static ceres::CostFunction *Create(SE3d pose, double weight = 1)
    {
        return (new ceres::AutoDiffCostFunction<PoseError, 7, 7>(new PoseError(pose, weight)));
    }

private:
    SE3d pose_;
    double weight_;
};

class RError
{
public:
    RError(SE3d pose) : pose_(pose) {}

    template <typename T>
    bool operator()(const T *pose, T *residuals) const
    {
        residuals[0] = (pose[0] - T(pose_.data()[0]));
        residuals[1] = (pose[1] - T(pose_.data()[1]));
        residuals[2] = (pose[2] - T(pose_.data()[2]));
        residuals[3] = (pose[3] - T(pose_.data()[3]));
        return true;
    }

    static ceres::CostFunction *Create(SE3d pose)
    {
        return (new ceres::AutoDiffCostFunction<RError, 4, 7>(new RError(pose)));
    }

private:
    SE3d pose_;
};

class TError
{
public:
    TError(SE3d pose, double weight) : pose_(pose), weight_(weight) {}

    template <typename T>
    bool operator()(const T *pose, T *residuals) const
    {
        residuals[0] = T(weight_) * (pose[4] - T(pose_.data()[4]));
        residuals[1] = T(weight_) * (pose[5] - T(pose_.data()[5]));
        residuals[2] = T(weight_) * (pose[6] - T(pose_.data()[6]));
        return true;
    }

    static ceres::CostFunction *Create(SE3d pose, double weight = 1)
    {
        return (new ceres::AutoDiffCostFunction<TError, 3, 7>(new TError(pose, weight)));
    }

private:
    SE3d pose_;
    double weight_;
};

class PoseErrorRPZ
{
public:
    PoseErrorRPZ(double *rpyxyz, double weight)
        : weight_(weight)
    {
        r_ = rpyxyz[1];
        p_ = rpyxyz[2];
        z_ = rpyxyz[5];
    }

    template <typename T>
    bool operator()(const T *r, const T *p, const T *z, T *residuals) const
    {
        residuals[0] = T(weight_) * (r[0] - T(r_));
        residuals[1] = T(weight_) * (p[0] - T(p_));
        residuals[2] = T(weight_) * (z[0] - T(z_));
        return true;
    }

    static ceres::CostFunction *Create(double *rpyxyz, double weight)
    {
        return (new ceres::AutoDiffCostFunction<PoseErrorRPZ, 3, 1, 1, 1>(new PoseErrorRPZ(rpyxyz, weight)));
    }

private:
    double r_, p_, z_;
    double weight_;
};

class PoseErrorYXY
{
public:
    PoseErrorYXY(double *rpyxyz, double weight)
        : weight_(weight)
    {
        Y_ = rpyxyz[0];
        x_ = rpyxyz[3];
        y_ = rpyxyz[4];
    }

    template <typename T>
    bool operator()(const T *Y, const T *x, const T *y, T *residuals) const
    {
        residuals[0] = T(weight_) * (Y[0] - T(Y_));
        residuals[1] = T(weight_) * (x[0] - T(x_));
        residuals[2] = T(weight_) * (y[0] - T(y_));
        return true;
    }

    static ceres::CostFunction *Create(double *rpyxyz, double weight)
    {
        return (new ceres::AutoDiffCostFunction<PoseErrorYXY, 3, 1, 1, 1>(new PoseErrorYXY(rpyxyz, weight)));
    }

private:
    double Y_, x_, y_;
    double weight_;
};

class RelocateRError
{
public:
    RelocateRError(SE3d relocated, SE3d unrelocated) : relocated_(relocated), unrelocated_(unrelocated) {}

    template <typename T>
    bool operator()(const T *r, T *residuals) const
    {
        T R[7] = {r[0], r[1], r[2], r[3], T(0), T(0), T(0)};
        T unrelocated[7];
        ceres::Cast(unrelocated_.data(), SE3d::num_parameters, unrelocated);
        T R_unrelocated[7];
        ceres::SE3Product(R, unrelocated, R_unrelocated);
        residuals[0] = T(relocated_.data()[0]) - R_unrelocated[0];
        residuals[1] = T(relocated_.data()[1]) - R_unrelocated[1];
        residuals[2] = T(relocated_.data()[2]) - R_unrelocated[2];
        residuals[3] = T(relocated_.data()[3]) - R_unrelocated[3];
        residuals[4] = T(relocated_.data()[4]) - R_unrelocated[4];
        residuals[5] = T(relocated_.data()[5]) - R_unrelocated[5];
        residuals[6] = T(relocated_.data()[6]) - R_unrelocated[6];
        return true;
    }

    static ceres::CostFunction *Create(SE3d relocated, SE3d unrelocated)
    {
        return (new ceres::AutoDiffCostFunction<RelocateRError, 7, 4>(new RelocateRError(relocated, unrelocated)));
    }

private:
    SE3d relocated_, unrelocated_;
};

} // namespace lvio_fusion

#endif // lvio_fusion_LOOP_ERROR_H
