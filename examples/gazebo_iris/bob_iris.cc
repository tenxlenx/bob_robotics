// BoB robotics includes
#include "common/main.h"
#include "common/gazebo_node.h"
#include "hid/joystick.h"
#include "robots/gazebo_uav.h"
// #include "robots/gazebo_tank.h"
#include "video/gazebocamerainput.h"
#include "video/display.h"

// Third-party includes
#include "third_party/units.h"

// Standard C++ includes
#include <chrono>
#include <iostream>
#include <thread>
#include <cstring>

using namespace BoBRobotics;
using namespace std::literals;
using namespace units::literals;
using namespace BoBRobotics::Video;

int
bob_main(int argc, char **argv)
{
    /************************************Gazebo setup************/

    // Create our node for publishing joystick values
    gazebo::transport::NodePtr node = getGazeboNode();

    /************************************Gazebo setup end************/
    std::unique_ptr<Display> display;
    std::unique_ptr<GazeboCameraInput> cam;
    if(argc >= 3) { // Initialize gazebo camera if more than 2 arguements are provided (display switch and camera url)
        std::cout << "Display switch enabled.\n";
        if(strcmp(argv[1], "-p") == 0) { 
            std::cout << "Using panoramic camera.\n";
            cam = std::make_unique<GazeboCameraInput>(node, argv[2], true);
            display = std::make_unique<Display>(*cam, cv::Size(640,320)); //unwrap resolution needs to be supplied
        }
        else if(strcmp(argv[1], "-s") == 0){
            std::cout << "Using simple camera.\n";
            cam = std::make_unique<GazeboCameraInput>(node, argv[2], false);
            display = std::make_unique<Display>(*cam); //unwrap resolution needs to be supplied
        }
        display->runInBackground();
    }
    Robots::GazeboQuadCopter iris(node); // QuadCopter agent
    HID::Joystick joystick(0.25f);
    iris.addJoystick(joystick);

    std::cout << "Iris connected and ready for liftoff!" << std::endl;

    do {
        // Check for joystick events
        if (!joystick.update()) {
            // A small delay so we don't hog CPU
            std::this_thread::sleep_for(5ms);
        }
    } while (!joystick.isPressed(HID::JButton::X));
    // Make sure to shut everything down.
    display->close();
    shutdownGazeboNode();
    std::cout <<"Shutting down...\n";

    return EXIT_SUCCESS;
}
