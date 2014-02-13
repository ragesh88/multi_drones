/*
 *
 *  Created on: Feb 6, 2014
 *      Author: Karol Hausman
 */

#include "ros/ros.h"
#include "geometry_msgs/Twist.h"
#include <tf/transform_broadcaster.h>
#include "multi_drone_ekf/EKF.h"


#include "multi_drone_ekf/Navdata.h"
#include "multi_drone_ekf/Tag.h"
#include "multi_drone_ekf/Tags.h"
#include <Eigen/Core>
#include <boost/bind.hpp>
struct Marker {

    std::string tags_topic_;
    tf::Transform tag_pose_;
    ros::NodeHandle nh_;
    ros::Subscriber sub_tags_;
    bool updated_;


    Marker(int marker_nr)
    {
        tags_topic_ = "/tags";
        updated_ = false;
        boost::function<void (const multi_drone_ekf::TagsConstPtr&)> tag_callback( boost::bind(&Marker::tagCB, this, _1, marker_nr) );
        sub_tags_ = nh_.subscribe(tags_topic_, 100,  tag_callback);
    }


    void tagCB(const multi_drone_ekf::TagsConstPtr& tag_msg, uint marker) {

        int tag_cnt = tag_msg->tag_count;

        if (tag_cnt == 0)
            return;

        for (int i = 0; i < tag_cnt; ++i) {
            if (marker == tag_msg->tags[i].id)
            ROS_INFO(
                    "Found tag  %i (cf: %.3f)", tag_msg->tags[i].id, tag_msg->tags[i].cf);
        }

        for (int i = 0; i < tag_cnt; ++i) {

            multi_drone_ekf::Tag tag = tag_msg->tags[i];

//            if (tag.id != marker) {
//                ROS_INFO("Detected unknown Marker");
//                return;
//            }

            // detection is too unsure
            if (tag.cf < 0.5)
                continue;


           double trans_x_ = tag.xMetric;
           double trans_y_ = tag.yMetric;
           double trans_z_ = tag.zMetric;
           double rot_z_ = -tag.yRot;
           double rot_y_ = -tag.xRot;
           double rot_x_ = tag.zRot;



            btVector3 trans_around_y(0,0,0);

            btQuaternion rot_around_y;
            rot_around_y.setEulerZYX(0,-M_PI/2,0);

            tf::Transform pose_around_y;


            pose_around_y.setOrigin(trans_around_y);
            pose_around_y.setRotation(rot_around_y);

            btVector3 translation(trans_x_,trans_y_,trans_z_);
            btQuaternion rotation;
            rotation.setEulerZYX(rot_z_, rot_y_,rot_x_);


            if (tag.id==marker){


                tag_pose_.setOrigin(translation);
                tag_pose_.setRotation(rotation);
                tag_pose_ = tag_pose_*pose_around_y;

            }
        }
        updated_ =true;
    }



};


struct Camera {

    ros::NodeHandle nh_;
    ros::Subscriber sub_tags_;
    tf::Transform tag_pose_;
    int counter_;
    double trans_x_, trans_y_, trans_z_;
    double rot_x_, rot_y_, rot_z_;
    int avg_number_;
    bool pose_set_;

    void tagCB(const multi_drone_ekf::TagsConstPtr& tag_msg, uint marker) {

        int tag_cnt = tag_msg->tag_count;

        if (tag_cnt == 0)
            return;

        for (int i = 0; i < tag_cnt; ++i) {
            if (marker == tag_msg->tags[i].id)
            ROS_INFO(
                    "Found tag  %i (cf: %.3f)", tag_msg->tags[i].id, tag_msg->tags[i].cf);
        }

        for (int i = 0; i < tag_cnt; ++i) {

            multi_drone_ekf::Tag tag = tag_msg->tags[i];



            // detection is too unsure

            if (tag.cf < 0.5)
                continue;


            trans_x_ += tag.xMetric;
            trans_y_ += tag.yMetric;
            trans_z_ += tag.zMetric;
            rot_z_ += -tag.yRot;
            rot_y_ += -tag.xRot;
            rot_x_ += tag.zRot;



            btVector3 trans_around_y(0,0,0);

            btQuaternion rot_around_y;
            rot_around_y.setEulerZYX(0,-M_PI/2,0);

            tf::Transform pose_around_y;


            pose_around_y.setOrigin(trans_around_y);
            pose_around_y.setRotation(rot_around_y);

            counter_ ++;

            if (counter_ == avg_number_)
            {

                btVector3 translation(trans_x_/avg_number_, trans_y_/avg_number_, trans_z_/avg_number_);


                btQuaternion rotation;

                rotation.setEulerZYX(rot_z_/avg_number_, rot_y_/avg_number_, rot_x_/avg_number_);


                if (tag.id == marker){
                    tag_pose_.setOrigin(translation);
                    tag_pose_.setRotation(rotation);
                    tag_pose_ = tag_pose_*pose_around_y;
                    pose_set_ = true;
                }
            }
        }
    }

    Camera(uint marker_nr) {

        pose_set_ = false;
        trans_x_ = 0; trans_y_ = 0; trans_z_ = 0;
        rot_x_ = 0; rot_y_ = 0; rot_z_ = 0;
        counter_ = 0;
        avg_number_ = 10;
        boost::function<void (const multi_drone_ekf::TagsConstPtr&)> tag_callback( boost::bind(&Camera::tagCB, this, _1, marker_nr) );
        sub_tags_ = nh_.subscribe("/tags", 100,  tag_callback);

    }


};

struct Ardrone {

	ros::NodeHandle nh_;
    ros::Subscriber sub_nav_;
    ros::Subscriber sub_tags_;
    ExtendedKalmanFilter kalman_filter_;

    tf::Transform tag_pose_;
    tf::Transform drone_in_marker_coord_;

    bool tag_seen_first_time_;
    bool navCB_done_;
    tf::Transform odom_pose_;
    double prevTime_;
    double last_yaw_;
    bool initialized_;
    double distZ;
    tf::Transform state_pose_;
    tf::Transform world_to_drone_pose_;
    tf::Transform world_to_cam_transform_;




    void tagCB(const multi_drone_ekf::TagsConstPtr& tag_msg, uint marker) {

		int tag_cnt = tag_msg->tag_count;

		if (tag_cnt == 0)
			return;

		for (int i = 0; i < tag_cnt; ++i) {
            if (marker == tag_msg->tags[i].id)
			ROS_INFO(
					"Found tag  %i (cf: %.3f)", tag_msg->tags[i].id, tag_msg->tags[i].cf);
		}

		for (int i = 0; i < tag_cnt; ++i) {

            multi_drone_ekf::Tag tag = tag_msg->tags[i];



			// detection is too unsure
			if (tag.cf < 0.5)
				continue;





           double trans_x_ = tag.xMetric;
           double trans_y_ = tag.yMetric;
           double trans_z_ = tag.zMetric;
           double rot_z_ = -tag.yRot;
           double rot_y_ = -tag.xRot;
           double rot_x_ = tag.zRot;



            btVector3 trans_around_y(0,0,0);

            btQuaternion rot_around_y;
            rot_around_y.setEulerZYX(0,-M_PI/2,0);

            tf::Transform pose_around_y;


            pose_around_y.setOrigin(trans_around_y);
            pose_around_y.setRotation(rot_around_y);

            btVector3 translation(trans_x_,trans_y_,trans_z_);
            btQuaternion rotation;
            rotation.setEulerZYX(rot_z_, rot_y_,rot_x_);


            if (tag.id==marker){


                tag_pose_.setOrigin(translation);
                tag_pose_.setRotation(rotation);
                tag_pose_ = tag_pose_*pose_around_y;

                tag_seen_first_time_ = true;

                if(initialized_)
                {
                    Eigen::Vector3f measurement;
                    measurement(0) = tag_pose_.getOrigin().getX();
                    measurement(1) = tag_pose_.getOrigin().getY();
                    measurement(2) = tag_pose_.getRotation().getZ();
                    kalman_filter_.correctionStep(measurement,world_to_cam_transform_,drone_in_marker_coord_,state_pose_,tag_pose_);
                }


            }
        }
    }



    void navCB(const multi_drone_ekf::NavdataConstPtr& nav_msg) {
        double dt;

        ROS_INFO_STREAM(
                "------------------------------------------ \n"
                << "Nav_msg vx: " << nav_msg->vx
                << "\nNav_msg vy: " << nav_msg->vy
                << "\nNav_msg vz: " << nav_msg->vz
                << "\nNav_msg yaw: " << nav_msg->rotZ
                << "\nNav_msg height: " << nav_msg->altd);

        if (prevTime_ == 0) {
            prevTime_ = nav_msg->header.stamp.toSec();
            dt = (double) 1 / 14;
        } else {
            dt = (nav_msg->header.stamp.toSec() - prevTime_);
            prevTime_ = nav_msg->header.stamp.toSec();
        }

        //Calculate changes of translation (incremental translation values)
        double distX = nav_msg->vx * dt / 1000.0;
        double distY = nav_msg->vy * dt / 1000.0;        
        distZ = (double) (nav_msg->altd) / 1000.0; //altd value in millimeter

        //Get absolute rotation values
        double yaw = ((nav_msg->rotZ) / (180.0 / M_PI));
        double pitch = ((nav_msg->rotY) / (180.0 / M_PI));
        double roll = ((nav_msg->rotX) / (180.0 / M_PI));



        Eigen::Vector3f odometry;

        odometry(0) = distX; // local position update
        odometry(1) = distY; // local position update
        odometry(2) = (yaw - last_yaw_) /*/ 180 * M_PI*/; // treat absolute value as incremental update

        last_yaw_ = yaw;



        if(initialized_)
        {
            if(!kalman_filter_.initialized_)
            {
                kalman_filter_.init(world_to_drone_pose_);
            }


            kalman_filter_.predictionStep(odometry);



            btQuaternion newRotation;
            newRotation.setEulerZYX(kalman_filter_.state_(2), pitch, roll);
            state_pose_.setRotation(newRotation);
            btVector3 newOrigin(kalman_filter_.state_(0),kalman_filter_.state_(1),distZ);
            state_pose_.setOrigin(newOrigin);
//        kalman_filter_.printState();

//        btVector3 newOrigin(kalman_filter_.state_pose_.getOrigin().getX()+kalman_filter_.state_(0),kalman_filter_.state_pose_.getOrigin().getY() + kalman_filter_.state_(1),kalman_filter_.state_pose_.getOrigin().getZ() + distZ);


//        btVector3 newOrigin(kalman_filter_.state_pose_.getOrigin().getX()+kalman_filter_.state_(0),kalman_filter_.state_pose_.getOrigin().getY() + kalman_filter_.state_(1),kalman_filter_.state_pose_.getOrigin().getZ() + distZ);


        }





//        btQuaternion newRotation;
//        newRotation.setEulerZYX(yaw, pitch, roll);
//        odom_pose_.setRotation(newRotation);

//        //Transform(rotate) local translation vector to global translation
//        btVector3 translation;
//        translation.setX(distX);
//        translation.setY(distY);
//        translation.setZ(0);

//        btMatrix3x3 rotationMatrix;
//        rotationMatrix.setEulerYPR(yaw, pitch, roll);
//        translation = rotationMatrix * translation;
//        translation.setZ(distZ-odom_pose_.getOrigin().getZ());

//        //Integrate/Sum translation values
//        btVector3 newOrigin(odom_pose_.getOrigin().getX() + translation.getX(), odom_pose_.getOrigin().getY() + translation.getY(), odom_pose_.getOrigin().getZ() + translation.getZ()); //holds integrated state
//        odom_pose_.setOrigin(newOrigin);




//        tf::TransformBroadcaster br;
//        br.sendTransform(
//        tf::StampedTransform(odom_pose_, nav_msg->header.stamp,
//                "/zeta_marker", "/ardrone"));

}





    Ardrone(uint marker_nr) {


        btVector3 translation(0,0,-0.15);
        btQuaternion rotation;
        rotation.setEulerZYX(-M_PI, 0,0);

        drone_in_marker_coord_.setOrigin(translation);
        drone_in_marker_coord_.setRotation(rotation);

        boost::function<void (const multi_drone_ekf::TagsConstPtr&)> tag_callback( boost::bind(&Ardrone::tagCB, this, _1, marker_nr) );
        sub_tags_ = nh_.subscribe("/tags", 100,  tag_callback);
        prevTime_ = 0;
        tag_seen_first_time_ = false;
        navCB_done_ = false;
        last_yaw_ = 0;
        initialized_ = false;
        distZ=0;



        sub_nav_ = nh_.subscribe("/ardrone/navdata", 100,
                &Ardrone::navCB, this);
	}

};

int main(int argc, char **argv) {
	ros::init(argc, argv, "ardrone_visualization");

	// To test your correction step:
	//	ExtendedKalmanFilter EKF;
	//	EKF.testFilter();
	//	return 0;

    Ardrone drone_observer(1);
    Camera camera(12);


	tf::TransformBroadcaster br;

    bool init = true;

	ros::Rate r(30);
    while (drone_observer.nh_.ok()) {
		ros::spinOnce();




        if (camera.pose_set_)
        {
//            br.sendTransform(
//                    tf::StampedTransform(camera.tag_pose_, ros::Time::now(),
//                            "/camera","/zeta_marker"));


            drone_observer.world_to_cam_transform_ = camera.tag_pose_.inverse();

            br.sendTransform(
                    tf::StampedTransform(drone_observer.world_to_cam_transform_, ros::Time::now(), "/zeta_marker",
                            "/camera"));

            br.sendTransform(
                    tf::StampedTransform(drone_observer.world_to_cam_transform_*drone_observer.tag_pose_, ros::Time::now(), "/zeta_marker",
                            "/beta_marker"));

//            br.sendTransform(
//            tf::StampedTransform(drone_observer.drone_in_marker_coord_, ros::Time::now()/*nav_msg->header.stamp*/,
//                    "/beta_marker", "/ardrone"));



            if(init)
            {

                    if(drone_observer.tag_seen_first_time_)
                    {

                        drone_observer.initialized_ = true;

                        init = false;

                        drone_observer.world_to_drone_pose_ = drone_observer.world_to_cam_transform_*drone_observer.tag_pose_*drone_observer.drone_in_marker_coord_;
//                  drone_observer.odom_pose_= camera.tag_pose_.inverse()*drone_observer.tag_pose_*drone_observer.drone_in_marker_coord_*drone_observer.odom_pose_;

//                    btVector3 newOrigin(drone_observer.kalman_filter_.state_pose_.getOrigin().getX()+drone_observer.kalman_filter_.state_(0),drone_observer.kalman_filter_.state_pose_.getOrigin().getY() + drone_observer.kalman_filter_.state_(1),drone_observer.kalman_filter_.state_pose_.getOrigin().getZ() + drone_observer.distZ);



//                    drone_observer.kalman_filter_.state_pose_= camera.tag_pose_.inverse()*drone_observer.tag_pose_*drone_observer.drone_in_marker_coord_*drone_observer.kalman_filter_.state_pose_;

//                    drone_observer.kalman_filter_.printState();

                    }
//                }
            }





//            br.sendTransform(
//            tf::StampedTransform(camera.tag_pose_.inverse()*drone_observer.tag_pose_*drone_observer.drone_in_marker_coord_, ros::Time::now()/*nav_msg->header.stamp*/,
//                    "/zeta_marker", "/ardrone"));


            br.sendTransform(
            tf::StampedTransform(drone_observer.world_to_drone_pose_, ros::Time::now()/*nav_msg->header.stamp*/,
                    "/zeta_marker", "/init"));


            br.sendTransform(
            tf::StampedTransform(drone_observer.state_pose_, ros::Time::now()/*nav_msg->header.stamp*/,
                    "/zeta_marker", "/ardrone"));


            br.sendTransform(
            tf::StampedTransform(drone_observer.world_to_cam_transform_.inverse()*drone_observer.state_pose_*drone_observer.drone_in_marker_coord_.inverse(), ros::Time::now()/*nav_msg->header.stamp*/,
                    "/camera", "/beta_from_H"));



        }

//        br.sendTransform(
//                tf::StampedTransform(drone_observer.tag_pose_, ros::Time::now(), "/camera",
//                        "/beta_marker"));



		r.sleep();
	}

	return 0;
}
