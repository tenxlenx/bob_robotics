/*
 * Copyright (C) 2016 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
#include <fcntl.h>
#include <functional>

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#include "GazeboQuadCopterPlugin.hh"
#include <gazebo/common/Assert.hh>
#include <gazebo/common/Plugin.hh>
#include <gazebo/msgs/msgs.hh>
#include <gazebo/sensors/sensors.hh>
#include <gazebo/transport/transport.hh>
#include <ignition/math/Filter.hh>
#include <mutex>
#include <sdf/sdf.hh>
#include <string>
#include <vector>

#define MAX_MOTORS 4

using namespace gazebo;

GZ_REGISTER_MODEL_PLUGIN(GazeboQuadCopterPlugin)

/// \brief Obtains a parameter from sdf.
/// \param[in] _sdf Pointer to the sdf object.
/// \param[in] _name Name of the parameter.
/// \param[out] _param Param Variable to write the parameter to.
/// \param[in] _default_value Default value, if the parameter not available.
/// \param[in] _verbose If true, gzerror if the parameter is not available.
/// \return True if the parameter was found in _sdf, false otherwise.
template<class T>
bool
getSdfParam(sdf::ElementPtr _sdf, const std::string &_name, T &_param, const T &_defaultValue, const bool &_verbose = false)
{
    if (_sdf->HasElement(_name)) {
        _param = _sdf->GetElement(_name)->Get<T>();
        return true;
    }

    _param = _defaultValue;
    if (_verbose) {
        gzerr << "[GazeboQuadCopterPlugin] Please specify a value for parameter ["
              << _name << "].\n";
    }
    return false;
}



double Rotor::kDefaultRotorVelocitySlowdownSim = 10.0;
double Rotor::kDefaultFrequencyCutoff = 5.0;
double Rotor::kDefaultSamplingRate = 0.2;

////////////////////////////////////////////////////////////////////////////////
GazeboQuadCopterPlugin::GazeboQuadCopterPlugin()
  : dataPtr(new GazeboQuadCopterPluginPrivate)
{
    this->dataPtr->connectionTimeoutCount = 0;
}

/////////////////////////////////////////////////
GazeboQuadCopterPlugin::~GazeboQuadCopterPlugin()
{
}

/////////////////////////////////////////////////
void
GazeboQuadCopterPlugin::Load(physics::ModelPtr _model, sdf::ElementPtr _sdf)
{
    GZ_ASSERT(_model, "GazeboQuadCopterPlugin _model pointer is null");
    GZ_ASSERT(_sdf, "GazeboQuadCopterPlugin _sdf pointer is null");

    this->dataPtr->model = _model;

    // per rotor
    if (_sdf->HasElement("rotor")) {
        sdf::ElementPtr rotorSDF = _sdf->GetElement("rotor");

        while (rotorSDF) {
            Rotor rotor;

            if (rotorSDF->HasAttribute("id")) {
                rotor.id = rotorSDF->GetAttribute("id")->Get(rotor.id);
            } else {
                rotor.id = this->dataPtr->rotors.size();
                gzwarn << "id attribute not specified, use order parsed ["
                       << rotor.id << "].\n";
            }

            if (rotorSDF->HasElement("jointName")) {
                rotor.jointName = rotorSDF->Get<std::string>("jointName");
            } else {
                gzerr << "Please specify a jointName,"
                      << " where the rotor is attached.\n";
            }

            // Get the pointer to the joint.
            rotor.joint = _model->GetJoint(rotor.jointName);
            if (rotor.joint == nullptr) {
                gzerr << "Couldn't find specified joint ["
                      << rotor.jointName << "]. This plugin will not run.\n";
                return;
            }

            if (rotorSDF->HasElement("turningDirection")) {
                std::string turningDirection = rotorSDF->Get<std::string>(
                        "turningDirection");
                // special cases mimic from rotors_gazebo_plugins
                if (turningDirection == "cw")
                    rotor.multiplier = -1;
                else if (turningDirection == "ccw")
                    rotor.multiplier = 1;
                else {
                    gzdbg << "not string, check turningDirection as float\n";
                    rotor.multiplier = rotorSDF->Get<double>("turningDirection");
                }
            } else {
                rotor.multiplier = 1;
                gzerr << "Please specify a turning"
                      << " direction multiplier ('cw' or 'ccw'). Default 'ccw'.\n";
            }

            getSdfParam<double>(rotorSDF, "rotorVelocitySlowdownSim", rotor.rotorVelocitySlowdownSim, 1);

            if (ignition::math::equal(rotor.rotorVelocitySlowdownSim, 0.0)) {
                gzerr << "rotor for joint [" << rotor.jointName
                      << "] rotorVelocitySlowdownSim is zero,"
                      << " aborting plugin.\n";
                return;
            }

            getSdfParam<double>(rotorSDF, "frequencyCutoff", rotor.frequencyCutoff, rotor.frequencyCutoff);
            getSdfParam<double>(rotorSDF, "samplingRate", rotor.samplingRate, rotor.samplingRate);

            // use ignition::math::Filter
            rotor.velocityFilter.Fc(rotor.frequencyCutoff, rotor.samplingRate);

            // initialize filter to zero value
            rotor.velocityFilter.Set(0.0);

            // note to use this
            // rotorVelocityFiltered = velocityFilter.Process(rotorVelocityRaw);

            // Overload the PID parameters if they are available.
            double param;
            getSdfParam<double>(rotorSDF, "vel_p_gain", param, rotor.pid.GetPGain());
            rotor.pid.SetPGain(param);

            getSdfParam<double>(rotorSDF, "vel_i_gain", param, rotor.pid.GetIGain());
            rotor.pid.SetIGain(param);

            getSdfParam<double>(rotorSDF, "vel_d_gain", param, rotor.pid.GetDGain());
            rotor.pid.SetDGain(param);

            getSdfParam<double>(rotorSDF, "vel_i_max", param, rotor.pid.GetIMax());
            rotor.pid.SetIMax(param);

            getSdfParam<double>(rotorSDF, "vel_i_min", param, rotor.pid.GetIMin());
            rotor.pid.SetIMin(param);

            getSdfParam<double>(rotorSDF, "vel_cmd_max", param, rotor.pid.GetCmdMax());
            rotor.pid.SetCmdMax(param);

            getSdfParam<double>(rotorSDF, "vel_cmd_min", param, rotor.pid.GetCmdMin());
            rotor.pid.SetCmdMin(param);

            // set pid initial command
            rotor.pid.SetCmd(0);

            this->dataPtr->rotors.push_back(rotor);
            rotorSDF = rotorSDF->GetNextElement("rotor");
        }
    }
    // Get sensors
    std::string imuName;
    getSdfParam<std::string>(_sdf, "imuName", imuName, "imu_sensor");
    std::string imuScopedName = this->dataPtr->model->GetWorld()->Name() + "::" + this->dataPtr->model->GetScopedName() + "::" + imuName;
    this->dataPtr->imuSensor = std::dynamic_pointer_cast<sensors::ImuSensor>(sensors::SensorManager::Instance()->GetSensor(imuScopedName));

    if (!this->dataPtr->imuSensor) {
        gzerr << "imu_sensor [" << imuScopedName
              << "] not found, abort GazeboQuadCopter plugin.\n"
              << "\n";
        return;
    }

    // Controller time control.
    this->dataPtr->lastControllerUpdateTime = 0;

    // Missed update count before we declare GazeboQuadCopterOnline status false
    getSdfParam<int>(_sdf, "connectionTimeoutMaxCount", this->dataPtr->connectionTimeoutMaxCount, 10);

    // Listen to the update event. This event is broadcast every simulation
    // iteration.
    // this->dataPtr->updateConnection = event::Events::ConnectWorldUpdateBegin(std::bind(&GazeboQuadCopterPlugin::OnUpdate, this));
    // Create the m_Node
    m_Node = transport::NodePtr(new transport::Node());
#if GAZEBO_MAJOR_VERSION < 8
    m_Node->Init(model->GetWorld()->GetName());
#else
    m_Node->Init(_model->GetWorld()->Name());
#endif

    // Create a topic name
    std::string topicName = "~/gazebo_quadcopter/motors_cmd";

    // Subscribe to the topic, and register a callback
    m_Sub = m_Node->Subscribe(topicName, &GazeboQuadCopterPlugin::OnMsg, this);
    std::cerr << "Subsribed to " << topicName << "\n";
    // Listen to the update event. This event is broadcast every simulation iteration.
    this->dataPtr->updateConnection = event::Events::ConnectWorldUpdateBegin(std::bind(&GazeboQuadCopterPlugin::OnUpdate, this));
    std::cout << "GazeboQuadCopter ready to fly. The force will be with you" << std::endl;
}

/////////////////////////////////////////////////
void
GazeboQuadCopterPlugin::OnMsg(ConstQuaternionPtr &_msg)
{
    std::lock_guard<std::mutex> lock(this->dataPtr->mutex);

    // gazebo::common::Time curTime = this->dataPtr->model->GetWorld()->SimTime();

    // std::cerr<<"receiving command"<<std::endl;
 
    this->dataPtr->rotors[0].cmd = this->dataPtr->rotors[0].maxRpm * _msg->w();
    this->dataPtr->rotors[1].cmd = this->dataPtr->rotors[1].maxRpm * _msg->x();
    this->dataPtr->rotors[2].cmd = this->dataPtr->rotors[2].maxRpm * _msg->y();
    this->dataPtr->rotors[3].cmd = this->dataPtr->rotors[3].maxRpm * _msg->z();
}
/////////////////////////////////////////////////
void GazeboQuadCopterPlugin::OnUpdate()
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);

  gazebo::common::Time curTime = this->dataPtr->model->GetWorld()->SimTime();

    this->ApplyMotorForces((curTime -this->dataPtr->lastControllerUpdateTime).Double());  
    this->dataPtr->lastControllerUpdateTime = curTime;
}
/////////////////////////////////////////////////
void
GazeboQuadCopterPlugin::ResetPIDs()
{
    // Reset velocity PID for rotors
    for (size_t i = 0; i < this->dataPtr->rotors.size(); ++i) {
        this->dataPtr->rotors[i].cmd = 0;
        // this->dataPtr->rotors[i].pid.Reset();
    }
}

/////////////////////////////////////////////////
void
GazeboQuadCopterPlugin::ApplyMotorForces(const double _dt)
{
    // update velocity PID for rotors and apply force to joint
    for (size_t i = 0; i < this->dataPtr->rotors.size(); ++i) {
        double velTarget = this->dataPtr->rotors[i].multiplier *
                           this->dataPtr->rotors[i].cmd /
                           this->dataPtr->rotors[i].rotorVelocitySlowdownSim;
        double vel = this->dataPtr->rotors[i].joint->GetVelocity(0);
        double error = vel - velTarget;
        double force = this->dataPtr->rotors[i].pid.Update(error, _dt);
        this->dataPtr->rotors[i].joint->SetForce(0, force);
        // std::cout<<force<<",";
    }
    // std::cout<<std::endl;
}

