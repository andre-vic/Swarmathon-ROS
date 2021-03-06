#include "JoystickGripperInterface.h"

#include <std_msgs/Float32.h> // For Gripper ROS messages

using namespace std;

// Default constructor so we can allocate space without providing parameters when needed
// For example declaring a variable of this type as a class member
JoystickGripperInterface::JoystickGripperInterface(){
    ready = false;
}

// Copy constructor
JoystickGripperInterface::JoystickGripperInterface(const JoystickGripperInterface& source){

    // Publisher state
    this->gripperWristAnglePublisher        = source.gripperWristAnglePublisher;
    this->gripperFingerAnglePublisher       = source.gripperFingerAnglePublisher;

    // QTimer state
    this->joystickGripperWristControlTimer  = source.joystickGripperWristControlTimer;
    this->joystickGripperFingerControlTimer = source.joystickGripperFingerControlTimer;

    // Wrist state
    this->wristAngle                        = source.wristAngle;
    this->wristAngleChangeRate              = source.wristAngleChangeRate;
    this->wristAngleMax                     = source.wristAngleMax;
    this->wristAngleMin                     = source.wristAngleMin;
    this->wristJoystickVector               = source.wristJoystickVector;

    // Finger state
    this->fingerAngle                       = source.fingerAngle;
    this->fingerAngleChangeRate             = source.fingerAngleChangeRate;
    this->fingerAngleMax                    = source.fingerAngleMax;
    this->fingerAngleMin                    = source.fingerAngleMin;
    this->fingerJoystickVector              = source.fingerJoystickVector;

    // Other
    this->commandReapplyRate                = source.commandReapplyRate;
    this->stickCenterTolerance              = source.stickCenterTolerance;

    this->nh                                = source.nh;
}

// The constructor needs a handle to the current ROS node and the name of the rover to control
// in order to setup the ROS command publishers.
JoystickGripperInterface::JoystickGripperInterface(ros::NodeHandle nh, string roverName){
    ready = false;

    this->nh = nh;

    // Joystick auto repeat for gripper control
    joystickGripperWristControlTimer = new QTimer(this);
    joystickGripperFingerControlTimer = new QTimer(this);

    // Connect the command timers to their handlers
    connect(joystickGripperWristControlTimer, SIGNAL(timeout()), this, SLOT(joystickGripperWristControlTimerEventHandler()));
    connect(joystickGripperFingerControlTimer, SIGNAL(timeout()), this, SLOT(joystickGripperFingerControlTimerEventHandler()));

    // Initialize gripper angles in radians
    wristAngle = 0;
    fingerAngle = 0;

    // Setup angle change parameters - min and max are taken from the physical rover.
    // Should this be limited here or rely on the ROS command receivers to sensibly
    // handle commands that are out of bounds?
    wristAngleMin = 0.0;
    wristAngleMax = 1.0; // Rad

    fingerAngleMin = 0.0;
    fingerAngleMax = 2.0;

    // The fraction of the joystick output value by which to change the gripper angles
    // This should be tuned in accordance with user feedback.
    fingerAngleChangeRate = 0.1;
    wristAngleChangeRate = 0.1;

    // Representation of the desired movement speed and direction generated by the joystick
    fingerJoystickVector = 0.0;
    wristJoystickVector = 0.0;

    commandReapplyRate = 100; // in milliseconds

    stickCenterTolerance = 0.05; // How close to zero does output from the joystick have to be for
    // us to consider the user to have centered the stick

    // Setup the gripper angle command publishers
    gripperWristAnglePublisher = nh.advertise<std_msgs::Float32>("/"+roverName+"/wristAngle", 10, this);
    gripperFingerAnglePublisher = nh.advertise<std_msgs::Float32>("/"+roverName+"/fingerAngle", 10, this);

    this->roverName = roverName;

    ready = true;
}

// Receives input from the joystick representing the direction and speed with which to move the wrist
// Records that desired motion and begins the command timer that cacluates and sends the movement commands
// to the appropriate ROS topics.
void JoystickGripperInterface::moveWrist(float vec) {
    if (!ready) throw JoystickGripperInterfaceNotReadyException();

    wristJoystickVector = -vec; // negate to make down the positive angle

    // Check whether the stick is near the center deadzone - if so stop issuing movement commands
    // if not apply the movement indicated by vec
    if (fabs(wristJoystickVector) < stickCenterTolerance) {
        joystickGripperWristControlTimer->stop();
    } else {
        // The event handler calculates the new angle for the wrist and sends it to the gripper wrist control topic
        joystickGripperWristControlTimer->start(commandReapplyRate);
    }


}


// Receives input from the joystick representing the direction and speed with which to move the fingers
// Records that desired motion and begins the command timer that cacluates and sends the movement commands
// to the appropriate ROS topics.
void JoystickGripperInterface::moveFingers(float vec){
    if (!ready) throw JoystickGripperInterfaceNotReadyException();

    fingerJoystickVector = vec;

    // Check whether the stick is near the center deadzone - if so stop issuing movement commands
    // if not apply the movement indicated by vec
    if (fabs(fingerJoystickVector) < stickCenterTolerance) {
        joystickGripperFingerControlTimer->stop();
    } else {
        // The event handler calculates the new angle for the wrist and sends it to the gripper wrist control topic
        joystickGripperFingerControlTimer->start(commandReapplyRate);
    }


}

// Update and broadcast gripper wrist commands
// Called by event timer so the commands continue to be generated even when the joystick
// is not moving
void JoystickGripperInterface::joystickGripperWristControlTimerEventHandler(){

    // Calculate the new wrist angle to request
    wristAngle += wristJoystickVector*wristAngleChangeRate;

    // Don't exceed the min and max angles
    if (wristAngle > wristAngleMax) wristAngle = wristAngleMax;
    else if (wristAngle < wristAngleMin) wristAngle = wristAngleMin;

    // If the wrist angle is small enough to use negative exponents set to zero
    // negative exponents confuse the downstream conversion to string
    if (fabs(wristAngle) < 0.001) wristAngle = 0.0f;

    std_msgs::Float32 angleMsg;
    angleMsg.data = wristAngle;
    gripperWristAnglePublisher.publish(angleMsg);

}

void JoystickGripperInterface::changeRovers(string roverName)
{
   ready = false;
    joystickGripperWristControlTimer->stop();
    joystickGripperFingerControlTimer->stop();

    gripperWristAnglePublisher.shutdown();
    gripperFingerAnglePublisher.shutdown();


    // Reset the gripper angles
    fingerAngle = 0;
    wristAngle = 0;

    // Reset joystick commands
    fingerJoystickVector = 0.0;
    wristJoystickVector = 0.0;

    this->roverName = roverName;

    // Setup the gripper angle command publishers
    gripperWristAnglePublisher = nh.advertise<std_msgs::Float32>("/"+roverName+"/wristAngle", 10, this);
    gripperFingerAnglePublisher = nh.advertise<std_msgs::Float32>("/"+roverName+"/fingerAngle", 10, this);
  ready = true;
}

// Update and broadcast gripper commands for the fingers
// Called by event timer so the commands continue to be generated even when the joystick
// is not moving
void JoystickGripperInterface::joystickGripperFingerControlTimerEventHandler(){
    // Calculate the new wrist angle to request
    fingerAngle += fingerJoystickVector*fingerAngleChangeRate;

    // Don't exceed the min and max angles
    if (fingerAngle > fingerAngleMax) fingerAngle = fingerAngleMax;
    else if (fingerAngle < fingerAngleMin) fingerAngle = fingerAngleMin;


    // If the finger angle is small enough to use negative exponents set to zero
    // negative exponents confuse the downstream conversion to string
    if (fabs(fingerAngle) < 0.001) fingerAngle = 0.0f;

    // Publish the finger angle commands
    std_msgs::Float32 angleMsg;
    angleMsg.data = fingerAngle;
    gripperFingerAnglePublisher.publish(angleMsg);

}

// Clean up the publishers when this object is destroyed
JoystickGripperInterface::~JoystickGripperInterface(){
    ready = false;

    if(joystickGripperWristControlTimer) delete joystickGripperWristControlTimer;
    if(joystickGripperFingerControlTimer) delete joystickGripperFingerControlTimer;

    gripperWristAnglePublisher.shutdown();
    gripperFingerAnglePublisher.shutdown();
}
