//Eigen
#include <Eigen/Core>

//OpenCV
#include <opencv2/core/core.hpp>
#include <opencv2/core/eigen.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

//PCL
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/visualization/common/common.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/filters/statistical_outlier_removal.h>

#include <chrono>
#include <iostream>

#include "estimate_motion.h"

using namespace p3dv;

bool MotionEstimator::estimate2D2D_E5P_RANSAC(frame_t &cur_frame_1, frame_t &cur_frame_2,
                                              std::vector<cv::DMatch> &matches, std::vector<cv::DMatch> &inlier_matches,
                                              Eigen::Matrix4f &T, double ransac_thre, double ransac_prob, bool show)
{
    std::chrono::steady_clock::time_point tic = std::chrono::steady_clock::now();

    std::vector<cv::Point2f> pointset1;
    std::vector<cv::Point2f> pointset2;

    for (int i = 0; i < (int)matches.size(); i++)
    {
        pointset1.push_back(cur_frame_1.keypoints[matches[i].queryIdx].pt);
        pointset2.push_back(cur_frame_2.keypoints[matches[i].trainIdx].pt);
    }

    cv::Mat camera_mat;
    cv::eigen2cv(cur_frame_1.K_cam, camera_mat);

    cv::Mat essential_matrix;

    cv::Mat inlier_matches_indicator;

    essential_matrix = cv::findEssentialMat(pointset1, pointset2, camera_mat,
                                            CV_RANSAC, ransac_prob, ransac_thre, inlier_matches_indicator);

    std::cout << "essential_matrix is " << std::endl
              << essential_matrix << std::endl;

    for (int i = 0; i < (int)matches.size(); i++)
    {
        if (inlier_matches_indicator.at<bool>(0, i) == 1)
        {
            inlier_matches.push_back(matches[i]);
        }
    }

    cv::Mat R;
    cv::Mat t;

    cv::recoverPose(essential_matrix, pointset1, pointset2, camera_mat, R, t, inlier_matches_indicator);

    std::chrono::steady_clock::time_point toc = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_used = std::chrono::duration_cast<std::chrono::duration<double>>(toc - tic);
    std::cout << "Estimate Motion [2D-2D] cost = " << time_used.count() << " seconds. " << std::endl;
    std::cout << "Find [" << inlier_matches.size() << "] inlier matches from [" << matches.size() << "] total matches." << std::endl;

    Eigen::Matrix3f R_eigen;
    Eigen::Vector3f t_eigen;
    cv::cv2eigen(R, R_eigen);
    cv::cv2eigen(t, t_eigen);
    T.block(0, 0, 3, 3) = R_eigen;
    T.block(0, 3, 3, 1) = t_eigen;
    T(3, 0) = 0;
    T(3, 1) = 0;
    T(3, 2) = 0;
    T(3, 3) = 1;

    std::cout << "Transform is " << std::endl
              << T << std::endl;

    if (show)
    {
        cv::Mat ransac_match_image;
        cv::namedWindow("RANSAC inlier matches", 0);
        cv::drawMatches(cur_frame_1.rgb_image, cur_frame_1.keypoints, cur_frame_2.rgb_image, cur_frame_2.keypoints, inlier_matches, ransac_match_image);
        cv::imshow("RANSAC inlier matches", ransac_match_image);
        cv::waitKey(0);
    }

    return 1;
}

bool MotionEstimator::estimate2D3D_P3P_RANSAC(frame_t &cur_frame, pointcloud_sparse_t &cur_map_3d, double ransac_thre,
                                              int iterationsCount, double ransac_prob, bool show)
{
    std::chrono::steady_clock::time_point tic = std::chrono::steady_clock::now();

    cv::Mat camera_mat;
    cv::eigen2cv(cur_frame.K_cam, camera_mat);

    //std::cout<< "K Mat:" <<std::endl<< camera_mat<<std::endl;

    std::vector<cv::Point2f> pointset2d;
    std::vector<cv::Point3f> pointset3d;
    std::vector<int> pointset3d_index;

    int count = 0;
    float dist_thre = 300;

    std::cout << "2D points: " << cur_frame.unique_pixel_ids.size() << std::endl
              << "3D points: " << cur_map_3d.unique_point_ids.size() << std::endl;

    // Construct the 2d-3d initial matchings
    for (int i = 0; i < cur_frame.unique_pixel_ids.size(); i++)
    {
        for (int j = 0; j < cur_map_3d.unique_point_ids.size(); j++)
        {
            if (cur_frame.unique_pixel_ids[i] == cur_map_3d.unique_point_ids[j])
            {

                float x_3d = cur_map_3d.rgb_pointcloud->points[j].x;
                float y_3d = cur_map_3d.rgb_pointcloud->points[j].y;
                float z_3d = cur_map_3d.rgb_pointcloud->points[j].z;
                float x_2d = cur_frame.keypoints[i].pt.x;
                float y_2d = cur_frame.keypoints[i].pt.y;

                if (std::abs(x_3d) < dist_thre && std::abs(y_3d) < dist_thre && std::abs(z_3d) < dist_thre)
                {
                    // Assign value for pointset2d and pointset3d
                    pointset2d.push_back(cv::Point2f(x_2d, y_2d));
                    //pointset2d.push_back(pixel2cam(cur_frame.keypoints[i].pt, camera_mat));

                    pointset3d.push_back(cv::Point3f(x_3d, y_3d, z_3d));
                    pointset3d_index.push_back(j);

                    count++;
                }
                //std::cout << "2D: " << cur_frame.unique_pixel_ids[i] << " " << cur_frame.keypoints[i].pt << std::endl;
                //std::cout << "3D: " << cur_map_3d.unique_point_ids[j] << " " << cv::Point3f(x_3d, y_3d, z_3d) << std::endl;
            }
        }
    }

    std::cout << count << " initial correspondences are used." << std::endl;

    // Use RANSAC P3P to estiamte the optimal transformation

    cv::Mat distort_para = cv::Mat::zeros(1, 4, CV_64FC1); // Assuming no lens distortion
    cv::Mat r_vec;
    cv::Mat t_vec;
    cv::Mat inliers;

    cv::solvePnPRansac(pointset3d, pointset2d, camera_mat, distort_para, r_vec, t_vec,
                       false, iterationsCount, ransac_thre, ransac_prob, inliers, cv::SOLVEPNP_EPNP);

    //cv::solvePnP(pointset3d, pointset2d, camera_mat, distort_para, r_vec, t_vec, false, cv::SOLVEPNP_EPNP);

    std::cout << "Inlier count: " << inliers.rows << std::endl;

    std::vector<int> outlier;
    for (int i = 0; i < inliers.rows; i++)
    {
        pointset3d_index[inliers.at<float>(i, 0)] = -1; // inlier 's index
    }

    for (int i = 0; i < pointset3d_index.size(); i++)
    {
        if (pointset3d_index[i] >= 0)
            outlier.push_back(pointset3d_index[i]);
    }
    for (int i = 0; i < outlier.size(); i++)
    {
        cur_map_3d.is_inlier[outlier[i]] = 0;
    }

    cv::Mat R_mat;
    cv::Rodrigues(r_vec, R_mat);

    Eigen::Matrix3f R_eigen;
    Eigen::Vector3f t_eigen;
    Eigen::Matrix4f T_mat;

    cv::cv2eigen(R_mat, R_eigen);
    cv::cv2eigen(t_vec, t_eigen);

    T_mat.block(0, 0, 3, 3) = R_eigen;
    T_mat.block(0, 3, 3, 1) = t_eigen;
    T_mat(3, 0) = 0;
    T_mat(3, 1) = 0;
    T_mat(3, 2) = 0;
    T_mat(3, 3) = 1;

    // std::cout << "Transform is: " << std::endl
    //           << T_mat << std::endl;

    cur_frame.pose_cam = T_mat;

    // Calculate the reprojection error
    std::vector<cv::Point2f> proj_points;
    cv::projectPoints(pointset3d, R_mat, t_vec, camera_mat, distort_para, proj_points);

    float reproj_err = 0.0;
    for (int i = 0; i < proj_points.size(); i++)
    {
        float cur_repro_error = norm(proj_points[i] - pointset2d[i]);
        reproj_err += cur_repro_error;

        //std::cout << cur_repro_error << std::endl;
    }

    reproj_err /= proj_points.size();
    double inlier_ratio = 1.0 * inliers.rows / count;
    std::cout << "Mean reprojection error: " << reproj_err << std::endl;

    std::chrono::steady_clock::time_point toc = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_used = std::chrono::duration_cast<std::chrono::duration<double>>(toc - tic);
    std::cout << "Estimate Motion [3D-2D] cost = " << time_used.count() << " seconds. " << std::endl;

    if (reproj_err > 10 && inlier_ratio < 0.5) // mean reprojection error is too big (may be some problem)
    {
        std::cout << "[Warning] pnp may encounter some problem, the inlier ratio is [ " << inlier_ratio * 100 << " % ], the mean reprojection error is [ " << reproj_err << " ]." << std::endl;
        return 0;
    }
    else
        return 1;
}

bool MotionEstimator::getDepthFast(frame_t &cur_frame_1, frame_t &cur_frame_2, Eigen::Matrix4f &T_21,
                                   const std::vector<cv::DMatch> &matches, double &appro_depth, int random_rate)
{
    cv::Mat T1_mat;
    cv::Mat T2_mat;
    cv::Mat camera_mat;

    Eigen::Matrix4f Teye = Eigen::Matrix4f::Identity();
    Eigen::Matrix<float, 3, 4> T1 = Teye.block<3, 4>(0, 0);
    Eigen::Matrix<float, 3, 4> T2 = (T_21 * Teye).block<3, 4>(0, 0);

    cv::eigen2cv(cur_frame_1.K_cam, camera_mat);
    cv::eigen2cv(T1, T1_mat);
    cv::eigen2cv(T2, T2_mat);

    std::vector<cv::Point2f> pointset1;
    std::vector<cv::Point2f> pointset2;

    for (int i = 0; i < matches.size(); i++)
    {
        if (i % random_rate == 0)
        {
            pointset1.push_back(pixel2cam(cur_frame_1.keypoints[matches[i].queryIdx].pt, camera_mat));
            pointset2.push_back(pixel2cam(cur_frame_2.keypoints[matches[i].trainIdx].pt, camera_mat));
        }
    }

    cv::Mat pts_3d_homo;
    if (pointset1.size() > 0)
        cv::triangulatePoints(T1_mat, T2_mat, pointset1, pointset2, pts_3d_homo);

    // De-homo and calculate mean depth
    double depth_sum = 0;
    for (int i = 0; i < pts_3d_homo.cols; i++)
    {
        cv::Mat pts_3d = pts_3d_homo.col(i);

        pts_3d /= pts_3d.at<float>(3, 0);

        Eigen::Vector3f pt_temp;
        pt_temp(0) = pts_3d.at<float>(0, 0);
        pt_temp(1) = pts_3d.at<float>(1, 0);
        pt_temp(2) = pts_3d.at<float>(2, 0);

        depth_sum = depth_sum + pt_temp.norm();
    }
    appro_depth = depth_sum / pts_3d_homo.cols;

    std::cout << "Mean relative depth is about " << appro_depth << " * baseline length. " << std::endl;
}

bool MotionEstimator::doTriangulation(frame_t &cur_frame_1, frame_t &cur_frame_2,
                                      const std::vector<cv::DMatch> &matches,
                                      pointcloud_sparse_t &sparse_pointcloud, bool show)
{
    std::chrono::steady_clock::time_point tic = std::chrono::steady_clock::now();

    cv::Mat T1_mat;
    cv::Mat T2_mat;
    cv::Mat camera_mat;

    Eigen::Matrix<float, 3, 4> T1 = cur_frame_1.pose_cam.block(0, 0, 3, 4);
    Eigen::Matrix<float, 3, 4> T2 = cur_frame_2.pose_cam.block(0, 0, 3, 4);

    cv::eigen2cv(cur_frame_1.K_cam, camera_mat);
    cv::eigen2cv(T1, T1_mat);
    cv::eigen2cv(T2, T2_mat);

    // std::cout<<camera_mat<<std::endl;
    // std::cout<<T1_mat<<std::endl;
    // std::cout<<T2_mat<<std::endl;

    std::vector<cv::Point2f> pointset1;
    std::vector<cv::Point2f> pointset2;

    int count_newly_triangu = 0;
    for (int i = 0; i < matches.size(); i++)
    {
        bool already_in_world = 0;
        for (int k = 0; k < sparse_pointcloud.unique_point_ids.size(); k++)
        {
            if (sparse_pointcloud.unique_point_ids[k] == cur_frame_1.unique_pixel_ids[matches[i].queryIdx])
            {
                already_in_world = 1;
                break;
            }
        }
        if (!already_in_world)
        {
            sparse_pointcloud.unique_point_ids.push_back(cur_frame_1.unique_pixel_ids[matches[i].queryIdx]);
            sparse_pointcloud.is_inlier.push_back(1);

            pointset1.push_back(pixel2cam(cur_frame_1.keypoints[matches[i].queryIdx].pt, camera_mat));
            pointset2.push_back(pixel2cam(cur_frame_2.keypoints[matches[i].trainIdx].pt, camera_mat));

            count_newly_triangu++;
        }
    }

    cv::Mat pts_3d_homo;
    if (pointset1.size() > 0)
        cv::triangulatePoints(T1_mat, T2_mat, pointset1, pointset2, pts_3d_homo);

    // De-homo and assign color
    for (int i = 0; i < pts_3d_homo.cols; i++)
    {
        cv::Mat pts_3d = pts_3d_homo.col(i);

        pts_3d /= pts_3d.at<float>(3, 0);

        pcl::PointXYZRGB pt_temp;
        pt_temp.x = pts_3d.at<float>(0, 0);
        pt_temp.y = pts_3d.at<float>(1, 0);
        pt_temp.z = pts_3d.at<float>(2, 0);

        // check here if(pt_temp.x> )

        cv::Point2f cur_key_pixel = cur_frame_1.keypoints[matches[i].queryIdx].pt;

        uchar blue = cur_frame_1.rgb_image.at<cv::Vec3b>(cur_key_pixel.y, cur_key_pixel.x)[0];
        uchar green = cur_frame_1.rgb_image.at<cv::Vec3b>(cur_key_pixel.y, cur_key_pixel.x)[1];
        uchar red = cur_frame_1.rgb_image.at<cv::Vec3b>(cur_key_pixel.y, cur_key_pixel.x)[2];

        pt_temp.r = 1.0 * red;
        pt_temp.g = 1.0 * green;
        pt_temp.b = 1.0 * blue;
        sparse_pointcloud.rgb_pointcloud->points.push_back(pt_temp);
    }

    std::cout << "Triangulate [ " << count_newly_triangu << " ] new points, [ " << sparse_pointcloud.rgb_pointcloud->points.size() << " ] points in total." << std::endl;

    std::chrono::steady_clock::time_point toc = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_used = std::chrono::duration_cast<std::chrono::duration<double>>(toc - tic);
    std::cout << "Triangularization done in " << time_used.count() << " seconds. " << std::endl;

    if (show)
    {
        // Show 2D image pair and correspondences
        cv::Mat match_image_pair;
        cv::drawMatches(cur_frame_1.rgb_image, cur_frame_1.keypoints, cur_frame_2.rgb_image, cur_frame_2.keypoints, matches, match_image_pair);
        cv::imshow("Triangularization matches", match_image_pair);
        cv::waitKey(0);

        boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer(new pcl::visualization::PCLVisualizer("Sfm Viewer"));
        viewer->setBackgroundColor(0, 0, 0);

        // Draw camera
        char t[256];
        std::string s;
        int n = 0;
        float frame_color_r, frame_color_g, frame_color_b;
        float sphere_size = 0.2;
        float line_size_cam = 0.4;

        pcl::PointXYZ pt_cam1(cur_frame_1.pose_cam(0, 3), cur_frame_1.pose_cam(1, 3), cur_frame_1.pose_cam(2, 3));
        pcl::PointXYZ pt_cam2(cur_frame_2.pose_cam(0, 3), cur_frame_2.pose_cam(1, 3), cur_frame_2.pose_cam(2, 3));

        sprintf(t, "%d", n);
        s = t;
        viewer->addSphere(pt_cam1, sphere_size, 1.0, 0.0, 0.0, s);
        n++;

        sprintf(t, "%d", n);
        s = t;
        viewer->addSphere(pt_cam2, sphere_size, 0.0, 0.0, 1.0, s);
        n++;

        sprintf(t, "%d", n);
        s = t;
        viewer->addLine(pt_cam1, pt_cam2, 0.0, 1.0, 0.0, s);
        n++;

        // for (int i = 0; i < sparse_pointcloud->points.size(); i++)
        // {
        //     char sparse_point[256];
        //     pcl::PointXYZ ptc_temp;
        //     ptc_temp.x = sparse_pointcloud->points[i].x;
        //     ptc_temp.y = sparse_pointcloud->points[i].y;
        //     ptc_temp.z = sparse_pointcloud->points[i].z;
        //     sprintf(sparse_point, "SP_%03u", i);
        //     viewer->addSphere(ptc_temp, 0.2, 1.0, 0.0, 0.0, sparse_point);
        // }

        viewer->addPointCloud(sparse_pointcloud.rgb_pointcloud, "sparsepointcloud");

        std::cout << "Click X(close) to continue..." << std::endl;
        while (!viewer->wasStopped())
        {
            viewer->spinOnce(100);
            boost::this_thread::sleep(boost::posix_time::microseconds(100000));
        }
    }

    std::cout << "Generate new sparse point cloud done." << std::endl;
    return true;
}

bool MotionEstimator::doUnDistort(frame_t &cur_frame, cv::Mat distort_coeff)
{
    cv::Mat camera_mat;
    cv::eigen2cv(cur_frame.K_cam, camera_mat);
    cv::Mat undistorted_img;
    cv::undistort(cur_frame.rgb_image, undistorted_img, camera_mat, distort_coeff);
    cur_frame.rgb_image = undistorted_img;

    std::cout << "Undistort the image done." << std::endl;
    return true;
}

/**
* \brief Transform a Point Cloud using a given transformation matrix
* \param[in]  cloud_in : A pointer of the Point Cloud before transformation
* \param[out] cloud_out : A pointer of the Point Cloud after transformation
* \param[in]  trans : A 4*4 transformation matrix
*/
bool MotionEstimator::transformCloud(pcl::PointCloud<pcl::PointXYZ>::Ptr &cloud_in,
                                     pcl::PointCloud<pcl::PointXYZ>::Ptr &cloud_out,
                                     Eigen::Matrix4f &trans)
{
    Eigen::Matrix4Xf PC;
    Eigen::Matrix4Xf TPC;
    PC.resize(4, cloud_in->size());
    TPC.resize(4, cloud_in->size());
    for (int i = 0; i < cloud_in->size(); i++)
    {
        PC(0, i) = cloud_in->points[i].x;
        PC(1, i) = cloud_in->points[i].y;
        PC(2, i) = cloud_in->points[i].z;
        PC(3, i) = 1;
    }
    TPC = trans * PC;
    for (int i = 0; i < cloud_in->size(); i++)
    {
        pcl::PointXYZ pt;
        pt.x = TPC(0, i);
        pt.y = TPC(1, i);
        pt.z = TPC(2, i);
        cloud_out->points.push_back(pt);
    }
    //cout << "Transform done ..." << endl;
}

bool MotionEstimator::outlierFilter(pointcloud_sparse_t &sparse_pointcloud, int MeanK, double std)
{
    // Create the filtering object
    pcl::StatisticalOutlierRemoval<pcl::PointXYZRGB> sor;

    std::vector<int> filtered_indice;

    sor.setInputCloud(sparse_pointcloud.rgb_pointcloud);
    sor.setMeanK(MeanK);         //50
    sor.setStddevMulThresh(std); //1.0
    sor.filter(filtered_indice);

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr output_pointcloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    std::vector<int> output_unique_points_id;
    std::vector<int> output_is_inlier;
    for (int i = 0; i < filtered_indice.size(); i++)
    {
        output_pointcloud->points.push_back(sparse_pointcloud.rgb_pointcloud->points[filtered_indice[i]]);
        output_unique_points_id.push_back(sparse_pointcloud.unique_point_ids[filtered_indice[i]]);
        output_is_inlier.push_back(sparse_pointcloud.is_inlier[filtered_indice[i]]);
    }

    std::cout << "apply outlier filter: [ " << sparse_pointcloud.unique_point_ids.size() << " ] points before filtering, [ " << filtered_indice.size() << " ] points after filtering." << std::endl;

    sparse_pointcloud.rgb_pointcloud->points.swap(output_pointcloud->points);
    sparse_pointcloud.unique_point_ids.swap(output_unique_points_id);
    sparse_pointcloud.is_inlier.swap(output_is_inlier);

    return 1;
}

bool MotionEstimator::estimateE8Points(std::vector<cv::KeyPoint> &keypoints1,
                                       std::vector<cv::KeyPoint> &keypoints2,
                                       std::vector<cv::DMatch> &matches,
                                       Eigen::Matrix3f &K,
                                       Eigen::Matrix4f &T)
{
#if 0
    std::vector<cv::Point2f> pointset1;
    std::vector<cv::Point2f> pointset2;

    for (int i = 0; i < (int)matches.size(); i++)
    {
        pointset1.push_back(keypoints1[matches[i].queryIdx].pt);
        pointset2.push_back(keypoints2[matches[i].trainIdx].pt);
    }

    cv::Point2d principal_point(K(0, 2), K(1, 2));
    double focal_length = (K(0, 0) + K(1, 1)) * 0.5;
    cv::Mat essential_matrix;
    essential_matrix = cv::findEssentialMat(pointset1, pointset2, focal_length, principal_point);
    std::cout << "essential_matrix is " << std::endl
              << essential_matrix << std::endl;

    cv::Mat R;
    cv::Mat t;

    cv::recoverPose(essential_matrix, pointset1, pointset2, R, t, focal_length, principal_point);

    Eigen::Matrix3f R_eigen;
    Eigen::Vector3f t_eigen;
    cv::cv2eigen(R, R_eigen);
    cv::cv2eigen(t, t_eigen);
    T.block(0, 0, 3, 3) = R_eigen;
    T.block(0, 3, 3, 1) = t_eigen;
    T(3, 0) = 0;
    T(3, 1) = 0;
    T(3, 2) = 0;
    T(3, 3) = 1;

    std::cout << "Transform is " << std::endl
              << T << std::endl;
#endif
}