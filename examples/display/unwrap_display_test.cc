/*
 * A test for the Video::Display class with panoramic camera. Build with make.
 *
 * Quit by pressing escape.
 */

#include "video/panoramic.h"
#include "video/display.h"

using namespace BoBRobotics::Video;

int main()
{
    auto cam = getPanoramicCamera();
    Display display(*cam, {1240, 600});
    display.run();
}