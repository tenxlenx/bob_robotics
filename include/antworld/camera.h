#pragma once

// BoB robotics includes
#include "common/pose.h"
#include "video/opengl/opengl.h"
#include "renderer.h"

// OpenGL includes
#include <GL/glew.h>

// GLFW
#include <GLFW/glfw3.h>

// Standard C++ includes
#include <functional>
#include <memory>

namespace BoBRobotics {
namespace AntWorld {
class Camera
  : public Video::OpenGL
{
protected:
    using meter_t = units::length::meter_t;
    using degree_t = units::angle::degree_t;

public:
    Camera(GLFWwindow *window,
           Renderer &renderer,
           const cv::Size &renderSize);

    GLFWwindow *getWindow() const;
    bool isOpen() const;
    void setPose(const Pose3<meter_t, degree_t> &pose);
    void setPosition(meter_t x, meter_t y, meter_t z);
    void setAttitude(degree_t yaw, degree_t pitch, degree_t roll);
    bool update();

    template<typename LengthUnit = meter_t>
    Vector3<LengthUnit> getPosition() const
    {
        return convertUnitArray<LengthUnit>(m_Pose.position());
    }

    template<typename AngleUnit = degree_t>
    std::array<AngleUnit, 3> getAttitude() const
    {
        return convertUnitArray<AngleUnit>(m_Pose.attitude());
    }

    template<typename LengthUnit = meter_t, typename AngleUnit = degree_t>
    Pose3<LengthUnit, AngleUnit> getPose() const
    {
        return m_Pose;
    }

    // Virtuals
    virtual bool readFrame(cv::Mat &outFrame) override;

    static std::unique_ptr<GLFWwindow, std::function<void(GLFWwindow *)>> initialiseWindow(const cv::Size &size);

private:
    Pose3<meter_t, degree_t> m_Pose;
    GLFWwindow *m_Window;
    Renderer &m_Renderer;

}; // Camera
} // AntWorld
} // BoBRobotics