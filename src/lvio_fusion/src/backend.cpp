#include "lvio_fusion/backend.h"
#include "lvio_fusion/ceres/imu_error.hpp"
#include "lvio_fusion/ceres/visual_error.hpp"
#include "lvio_fusion/frontend.h"
#include "lvio_fusion/manager.h"
#include "lvio_fusion/map.h"
#include "lvio_fusion/utility.h"
#include "lvio_fusion/visual/feature.h"
#include "lvio_fusion/visual/landmark.h"

namespace lvio_fusion
{

Backend::Backend(double window_size, bool update_weights)
    : window_size_(window_size), update_weights_(update_weights)
{
    thread_ = std::thread(std::bind(&Backend::BackendLoop, this));
}

void Backend::UpdateMap()
{
    map_update_.notify_one();
}

void Backend::Pause()
{
    if (status == BackendStatus::RUNNING)
    {
        std::unique_lock<std::mutex> lock(pausing_mutex_);
        status = BackendStatus::TO_PAUSE;
        pausing_.wait(lock);
    }
}

void Backend::Continue()
{
    if (status == BackendStatus::PAUSING)
    {
        status = BackendStatus::RUNNING;
        running_.notify_one();
    }
}

void Backend::BackendLoop()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(running_mutex_);
        if (status == BackendStatus::TO_PAUSE)
        {
            status = BackendStatus::PAUSING;
            pausing_.notify_one();
            running_.wait(lock);
        }
        map_update_.wait(lock);
        auto t1 = std::chrono::steady_clock::now();
        Optimize();
        auto t2 = std::chrono::steady_clock::now();
        auto time_used = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);
        LOG(INFO) << "Backend cost time: " << time_used.count() << " seconds.";
    }
}
 bool showIMUError(const double*  parameters0, const double*  parameters1, const double*  parameters2, const double*  parameters3, const double*  parameters4, const double*  parameters5, const double*  parameters6, const double*  parameters7,  imu::Preintegration::Ptr preintegration_, double time)  
    {
        Quaterniond Qi(parameters0[3], parameters0[0], parameters0[1], parameters0[2]);
        Vector3d Pi(parameters0[4], parameters0[5], parameters0[6]);

        Vector3d Vi(parameters1[0], parameters1[1], parameters1[2]);
        Vector3d Bai(parameters2[0], parameters2[1], parameters2[2]);
        Vector3d Bgi(parameters3[0], parameters3[1], parameters3[2]);

        Quaterniond Qj(parameters4[3], parameters4[0], parameters4[1], parameters4[2]);
        Vector3d Pj(parameters4[4], parameters4[5], parameters4[6]);

        Vector3d Vj(parameters5[0], parameters5[1], parameters5[2]);
        Vector3d Baj(parameters6[0], parameters6[1], parameters6[2]);
        Vector3d Bgj(parameters7[0], parameters7[1], parameters7[2]);
        Matrix<double, 15, 1> residual;
        residual = preintegration_->Evaluate(Pi, Qi, Vi, Bai, Bgi, Pj, Qj, Vj, Baj, Bgj);
        Matrix<double, 15, 15> sqrt_info = LLT<Matrix<double, 15, 15>>(preintegration_->covariance.inverse()).matrixL().transpose();
    
        residual = sqrt_info * residual;
         LOG(INFO)<<"time"<<time<<"   residual  "<<residual.transpose();
         return true;
    }
void Backend::BuildProblem(Frames &active_kfs, adapt::Problem &problem,bool isimu)
{
    ceres::LossFunction *loss_function = new ceres::HuberLoss(1.0);
    ceres::LocalParameterization *local_parameterization = new ceres::ProductParameterization(
        new ceres::EigenQuaternionParameterization(),
        new ceres::IdentityParameterization(3));

    double start_time = active_kfs.begin()->first;

    for (auto &pair_kf : active_kfs)
    {
        auto frame = pair_kf.second;
        double *para_kf = frame->pose.data();
        problem.AddParameterBlock(para_kf, SE3d::num_parameters, local_parameterization);
        for (auto &pair_feature : frame->features_left)
        {
            auto feature = pair_feature.second;
            auto landmark = feature->landmark.lock();
            auto first_frame = landmark->FirstFrame().lock();
            ceres::CostFunction *cost_function;
            if (first_frame->time < start_time)
            {
                cost_function = PoseOnlyReprojectionError::Create(cv2eigen(feature->keypoint), landmark->ToWorld(), Camera::Get(), frame->weights.visual);
                problem.AddResidualBlock(ProblemType::PoseOnlyReprojectionError, cost_function, loss_function, para_kf);
            }
            else if (first_frame != frame)
            {
                double *para_fist_kf = first_frame->pose.data();
                cost_function = TwoFrameReprojectionError::Create(landmark->position, cv2eigen(feature->keypoint), Camera::Get(), frame->weights.visual);
                problem.AddResidualBlock(ProblemType::TwoFrameReprojectionError, cost_function, loss_function, para_fist_kf, para_kf);
            }
        }
    }
 //NEWADD
    if (Imu::Num() && initializer_->initialized&&isimu)
    {
        Frame::Ptr last_frame;
        Frame::Ptr current_frame;
        bool first=true;
     
        for (auto kf_pair : active_kfs)
        {
            current_frame = kf_pair.second;
            if (!current_frame->bImu||!current_frame->last_keyframe||current_frame->preintegration==nullptr)
            {
                last_frame=current_frame;
               continue;
            }
            auto para_kf = current_frame->pose.data();
            auto para_v = current_frame->Vw.data();
            auto para_bg = current_frame->ImuBias.linearized_bg.data();
            auto para_ba = current_frame->ImuBias.linearized_ba.data();
            problem.AddParameterBlock(para_v, 3);
            problem.AddParameterBlock(para_ba, 3);
            problem.AddParameterBlock(para_bg, 3);

            if (last_frame && last_frame->bImu&&last_frame->last_keyframe)
            {
                auto para_kf_last = last_frame->pose.data();
                auto para_v_last = last_frame->Vw.data();
                auto para_bg_last = last_frame->ImuBias.linearized_bg.data();//恢复
                auto para_ba_last =last_frame->ImuBias.linearized_ba.data();//恢复
                ceres::CostFunction *cost_function = ImuError::Create(current_frame->preintegration);
                problem.AddResidualBlock(ProblemType::IMUError,cost_function, NULL, para_kf_last, para_v_last,para_ba_last,para_bg_last, para_kf, para_v,para_ba,para_bg);
               showIMUError(para_kf_last, para_v_last,para_ba_last,para_bg_last, para_kf, para_v,para_ba,para_bg,current_frame->preintegration,current_frame->time-1.40364e+09+8.60223e+07);
            }
            last_frame = current_frame;
        }
    }
    //NEWADDEND
}

double compute_reprojection_error(Vector2d ob, Vector3d pw, SE3d pose, Camera::Ptr camera)
{
    Vector2d error(0, 0);
    PoseOnlyReprojectionError(ob, pw, camera, 1)(pose.data(), error.data());
    return error.norm();
}

void Backend::recoverData(Frames active_kfs,SE3d old_pose_imu)
{
    SE3d new_pose=active_kfs.begin()->second->pose;
    Vector3d origin_P0=old_pose_imu.translation();
    Vector3d origin_R0=R2ypr( old_pose_imu.rotationMatrix());
  Vector3d origin_R00=R2ypr(new_pose.rotationMatrix());
 double y_diff = origin_R0.x() - origin_R00.x();
   Matrix3d rot_diff = ypr2R(Vector3d(y_diff, 0, 0));
   if (abs(abs(origin_R0.y()) - 90) < 1.0 || abs(abs(origin_R00.y()) - 90) < 1.0)
        {
            rot_diff =old_pose_imu.rotationMatrix() *new_pose.inverse().rotationMatrix();
        }
        for(auto kf_pair : active_kfs){
            auto frame = kf_pair.second;
            if(!frame->preintegration||!frame->last_keyframe||!frame->bImu){
                    continue;
            }
            frame->SetPose(rot_diff * frame->pose.rotationMatrix(),rot_diff * (frame->pose.translation()-new_pose.translation())+origin_P0);
            frame->SetVelocity(rot_diff*frame->Vw);

            Bias bias_(frame->ImuBias.linearized_ba[0],frame->ImuBias.linearized_ba[1],frame->ImuBias.linearized_ba[2],frame->ImuBias.linearized_bg[0],frame->ImuBias.linearized_bg[1],frame->ImuBias.linearized_bg[2]);
            frame->SetNewBias(bias_);
           // LOG(INFO)<<"opt  TIME: "<<frame->time-1.40364e+09+8.60223e+07<<"    V  "<<frame->Vw.transpose()<<"    R  "<<frame->pose.rotationMatrix().eulerAngles(0,1,2).transpose()<<"    P  "<<frame->pose.translation().transpose();
        }
}

void Backend::Optimize()
{
    static double forward = 0;
    std::unique_lock<std::mutex> lock(mutex);
    Frames active_kfs = Map::Instance().GetKeyFrames(finished);
    LOG(INFO)<<"BACKEND IMU OPTIMIZER  ===>"<<active_kfs.size();

    if (active_kfs.empty())
        return;
    SE3d old_pose = (--active_kfs.end())->second->pose;
   SE3d old_pose_imu=active_kfs.begin()->second->pose;
    adapt::Problem problem;
    BuildProblem(active_kfs, problem);

    ceres::Solver::Options options;
    options.linear_solver_type = ceres::DENSE_SCHUR;
    options.trust_region_strategy_type = ceres::DOGLEG;
    options.max_solver_time_in_seconds = 0.6 * window_size_;
    options.num_threads = num_threads;
    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);
    LOG(INFO)<<summary.FullReport();
     //NEWADD
    if(Imu::Num()&&initializer_->initialized)
    {
        recoverData(active_kfs,old_pose_imu);
    }
    
    //NEWADDEND
    // reject outliers and clean the map
    for (auto &pair_kf : active_kfs)
    {
        auto frame = pair_kf.second;
        auto features_left = frame->features_left;
        for (auto &pair_feature : features_left)
        {
            auto feature = pair_feature.second;
            auto landmark = feature->landmark.lock();
            auto first_frame = landmark->FirstFrame().lock();
            if (frame != first_frame && compute_reprojection_error(cv2eigen(feature->keypoint), landmark->ToWorld(), frame->pose, Camera::Get()) > 10)
            {
                landmark->RemoveObservation(feature);
                frame->RemoveFeature(feature);
            }
            if (landmark->observations.size() <= 1 && frame->id != Frame::current_frame_id)
            {
                Map::Instance().RemoveLandmark(landmark);
            }
        }
    }

    if (Lidar::Num() && mapping_)
    {
        mapping_->Optimize(active_kfs);
    }

    if (Navsat::Num() && Navsat::Get()->initialized)
    {
        double start_time = Navsat::Get()->Optimize((--active_kfs.end())->first);
        if (start_time && mapping_)
        {
            Frames mapping_kfs = Map::Instance().GetKeyFrames(start_time);
            for (auto &pair : mapping_kfs)
            {
                mapping_->ToWorld(pair.second);
            }
        }
    }

    // propagate to the last frame
     new_frame=(--active_kfs.end())->second;
    SE3d new_pose = (--active_kfs.end())->second->pose;
    SE3d transform= new_pose * old_pose.inverse();
    forward = (--active_kfs.end())->first + epsilon;
    ForwardPropagate(transform, forward,old_pose);
    finished = forward - window_size_;
}
void InertialOptimization(Frames &key_frames,double priorG, double priorA,Frame::Ptr new_frame)   
{
    ceres::Problem problem;
    ceres::CostFunction *cost_function ;
    //先验BIAS约束
    auto para_gyroBias=key_frames.begin()->second->ImuBias.linearized_bg.data();
    problem.AddParameterBlock(para_gyroBias, 3);

    auto para_accBias=key_frames.begin()->second->ImuBias.linearized_ba.data();
    problem.AddParameterBlock(para_accBias, 3);

    //优化重力、BIAS和速度的边
    Quaterniond rwg(Matrix3d::Identity());
    SO3d RwgSO3(rwg);
    auto para_rwg=RwgSO3.data();
    
        ceres::LocalParameterization *local_parameterization = new ceres::EigenQuaternionParameterization();
    problem.AddParameterBlock(para_rwg, SO3d::num_parameters,local_parameterization);
    problem.SetParameterBlockConstant(para_rwg);
    Frame::Ptr last_frame=new_frame;
    Frame::Ptr current_frame;
    bool first=true;
    for(Frames::iterator iter = key_frames.begin(); iter != key_frames.end(); iter++)
    {
        current_frame=iter->second;
        if (!current_frame->last_keyframe||current_frame->preintegration==nullptr)
        {
            last_frame=current_frame;
            continue;
        }
        auto para_v = current_frame->Vw.data();
        problem.AddParameterBlock(para_v, 3);   

        if (last_frame)
        {
            auto para_v_last = last_frame->Vw.data();
             if(first){
                    problem.AddParameterBlock(para_v_last, 3);
                    problem.SetParameterBlockConstant(para_v_last);
                    first=false;
                }
            cost_function = ImuErrorG::Create(current_frame->preintegration,current_frame->pose,last_frame->pose,priorA,priorG);
            problem.AddResidualBlock(cost_function, NULL,para_v_last,para_accBias,para_gyroBias,para_v,para_rwg);
            //LOG(INFO)<<last_frame->time -1.40364e+09+8.60223e+07;
         }
        last_frame = current_frame;
    }

    ceres::Solver::Options options;
    options.linear_solver_type = ceres::DENSE_SCHUR;
      options.trust_region_strategy_type = ceres::DOGLEG;
    options.max_solver_time_in_seconds = 0.1;
   //  options.max_num_iterations =4;
  
    options.num_threads = 4;
    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);
     LOG(INFO)<<summary.BriefReport();
   // std::this_thread::sleep_for(std::chrono::seconds(3));

    //数据恢复
    Bias bias_(para_accBias[0],para_accBias[1],para_accBias[2],para_gyroBias[0],para_gyroBias[1],para_gyroBias[2]);
    for(Frames::iterator iter = key_frames.begin(); iter != key_frames.end(); iter++)
    {
        current_frame=iter->second;
        current_frame->SetNewBias(bias_);
    }
    return ;
}
void Backend::ForwardPropagate(SE3d transform, double time,SE3d old_pose)
{
    std::unique_lock<std::mutex> lock(frontend_.lock()->mutex);
    Frame::Ptr last_frame = frontend_.lock()->last_frame;
    Frames active_kfs = Map::Instance().GetKeyFrames(time);
    LOG(INFO)<<"BACKEND IMU ForwardPropagate  ===>"<<active_kfs.size();
    if (active_kfs.find(last_frame->time) == active_kfs.end())
    {
        active_kfs[last_frame->time] = last_frame;
    }
        //NEWADD
    double priorA=1e3;
    double priorG=1e1;
        if(Imu::Num() && initializer_->initialized){
             double dt=0;
            if(Tinit!=-1)
                 dt=(--active_kfs.end())->second->time-Tinit;
            if(dt>5&&!initA){
                initializer_->reinit=true;
                initA=true;
                priorA=1e4;
                priorG=1e1;
            }
            else if(dt>15&&!initB){
               initializer_->reinit=true;
                initB=true;
                priorA=0;
                priorG=0;
            }
        }

    Frames  frames_init;
     if (Imu::Num() &&( !initializer_->initialized||initializer_->reinit))
    {
        frames_init = Map::Instance().GetKeyFrames(0,time,initializer_->num_frames);
        LOG(INFO)<<frames_init.begin()->first -1.40364e+09+8.60223e+07<<"  "<<frontend_.lock()->validtime-1.40364e+09+8.60223e+07;
        if (frames_init.size() == initializer_->num_frames&&frames_init.begin()->first>frontend_.lock()->validtime&&frames_init.begin()->second->preintegration)
        {
            if(!initializer_->initialized){
                Tinit=(--frames_init.end())->second->time;
            }
            isInitliazing=true;
        }
    }
    bool isOriginInit=false;
    if (isInitliazing)
    {
     //   if(initializer_->bimu==false||initializer_->reinit==true)
            isOriginInit=true;
        LOG(INFO)<<"Initializer Start";
        if(initializer_->InitializeIMU(frames_init,priorA,priorG))
        {
            frontend_.lock()->status = FrontendStatus::TRACKING_GOOD;
            SE3d new_pose = (--frames_init.end())->second->pose;
            SE3d transform= new_pose * old_pose.inverse();
            for(auto kf:active_kfs){
                Frame::Ptr frame =kf.second;
                if(frame->preintegration!=nullptr)
                        frame->bImu=true;
            }
        }
        LOG(INFO)<<"Initiaclizer Finished";
        isInitliazing=false;
    }    
//NEWADDEND
    if(!isOriginInit)
     PoseGraph::Instance().Propagate(transform, active_kfs);

    adapt::Problem problem;
    BuildProblem(active_kfs, problem,false);

    ceres::Solver::Options options;
    options.linear_solver_type = ceres::DENSE_SCHUR;
    options.max_num_iterations = 1;
    options.num_threads = num_threads;
    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);
 //NEWADD
    if(Imu::Num() && initializer_->initialized)
    {

        Frame::Ptr last_key_frame=new_frame;
        bool first=true;
        for(auto kf:active_kfs){
            Frame::Ptr current_key_frame =kf.second;
                 Vector3d Gz ;
            Gz << 0, 0, -9.81007;
            double t12=current_key_frame->preintegration->sum_dt;
            Vector3d twb1=last_key_frame->GetImuPosition();
            Matrix3d Rwb1=last_key_frame->GetImuRotation();
            Vector3d Vwb1;
            Vwb1=last_key_frame->Vw;

            Matrix3d Rwb2=NormalizeRotation(Rwb1*current_key_frame->preintegration->GetDeltaRotation(last_key_frame->GetImuBias()).toRotationMatrix());
            Vector3d twb2=twb1 + Vwb1*t12 + 0.5f*t12*t12*Gz+ Rwb1*current_key_frame->preintegration->GetDeltaPosition(last_key_frame->GetImuBias());
            Vector3d Vwb2=Vwb1+t12*Gz+Rwb1*current_key_frame->preintegration->GetDeltaVelocity(last_key_frame->GetImuBias());
            current_key_frame->SetVelocity(Vwb2);
            current_key_frame->SetNewBias(last_key_frame->GetImuBias());
            last_key_frame=current_key_frame;
        }
    }


   if(Imu::Num()&&initializer_->initialized)
   {
            Frames active_kfs = Map::Instance().GetKeyFrames(time);

//       InertialOptimization(active_kfs,1e1,1e3,new_frame);
        adapt::Problem problem_;
        ceres::LocalParameterization *local_parameterization = new ceres::ProductParameterization(
        new ceres::EigenQuaternionParameterization(),
        new ceres::IdentityParameterization(3));


        Frame::Ptr last_frame=new_frame;
        Frame::Ptr current_frame;
        bool first=true;
        //int n=active_kfs.size();
        if(active_kfs.size()>0)
        for (auto kf_pair : active_kfs)
        {
            current_frame = kf_pair.second;
            if (!current_frame->bImu||!current_frame->last_keyframe||current_frame->preintegration==nullptr)
            {
                last_frame=current_frame;
               continue;
            }
            auto para_kf = current_frame->pose.data();
            auto para_v = current_frame->Vw.data();
            auto para_bg = current_frame->ImuBias.linearized_bg.data();
            auto para_ba = current_frame->ImuBias.linearized_ba.data();
            problem_.AddParameterBlock(para_kf, SE3d::num_parameters, local_parameterization);
            problem_.AddParameterBlock(para_v, 3);
            problem_.AddParameterBlock(para_ba, 3);
            problem_.AddParameterBlock(para_bg, 3);
            problem_.SetParameterBlockConstant(para_kf);
            if (last_frame && last_frame->bImu&&last_frame->last_keyframe)
            {
                auto para_kf_last = last_frame->pose.data();
                auto para_v_last = last_frame->Vw.data();
                auto para_bg_last = last_frame->ImuBias.linearized_bg.data();//恢复
                auto para_ba_last =last_frame->ImuBias.linearized_ba.data();//恢复
                if(first){
                    problem_.AddParameterBlock(para_kf_last, SE3d::num_parameters, local_parameterization);
                    problem_.AddParameterBlock(para_v_last, 3);
                    problem_.AddParameterBlock(para_bg_last, 3);
                    problem_.AddParameterBlock(para_ba_last, 3);
                    problem_.SetParameterBlockConstant(para_kf_last);
                    problem_.SetParameterBlockConstant(para_v_last);
                    problem_.SetParameterBlockConstant(para_bg_last);
                    problem_.SetParameterBlockConstant(para_ba_last);
                    first=false;
                }
                ceres::CostFunction *cost_function = ImuError::Create(current_frame->preintegration);
                problem_.AddResidualBlock(ProblemType::IMUError,cost_function, NULL, para_kf_last, para_v_last,para_ba_last,para_bg_last, para_kf, para_v,para_ba,para_bg);
                showIMUError(para_kf_last, para_v_last,para_ba_last,para_bg_last, para_kf, para_v,para_ba,para_bg,current_frame->preintegration,current_frame->time-1.40364e+09+8.60223e+07);

            }
            last_frame = current_frame;
        }
    ceres::Solver::Options options_;
    options_.linear_solver_type = ceres::DENSE_SCHUR;
    options_.trust_region_strategy_type = ceres::DOGLEG;
    options_.max_num_iterations =4;
    options_.max_solver_time_in_seconds=0.1;
    options_.num_threads = 4;
    ceres::Solver::Summary summary_;
    ceres::Solve(options_, &problem_, &summary_);
     LOG(INFO)<<"FORWARD  "<<summary_.BriefReport();
       for(auto kf_pair : active_kfs){
            auto frame = kf_pair.second;
            if(!frame->preintegration||!frame->last_keyframe||!frame->bImu){
                    continue;
            }
            Bias bias_(frame->ImuBias.linearized_ba[0],frame->ImuBias.linearized_ba[1],frame->ImuBias.linearized_ba[2],frame->ImuBias.linearized_bg[0],frame->ImuBias.linearized_bg[1],frame->ImuBias.linearized_bg[2]);
            frame->SetNewBias(bias_);
           // LOG(INFO)<<"opt  TIME: "<<frame->time-1.40364e+09+8.60223e+07<<"    V  "<<frame->Vw.transpose()<<"    R  "<<frame->pose.rotationMatrix().eulerAngles(0,1,2).transpose()<<"    P  "<<frame->pose.translation().transpose();
        }
            Map::Instance().mapUpdated=false;
        if(active_kfs.size()==0){
          frontend_.lock()->UpdateFrameIMU(new_frame->GetImuBias());
        }
        else{
          frontend_.lock()->UpdateFrameIMU((--active_kfs.end())->second->GetImuBias());
        }
   }
    //NEWADDEND

    frontend_.lock()->UpdateCache();
}

} // namespace lvio_fusion