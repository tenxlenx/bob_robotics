#ifndef _WIN32
// BoB robotics includes
#include "common/robo_claw.h"
#include "common/macros.h"


//----------------------------------------------------------------------------
// BoBRobotics::RoboClaw
//----------------------------------------------------------------------------
namespace BoBRobotics {
RoboClaw::RoboClaw(const char *path, uint8_t address, int maxRetry)
:   m_SerialInterface(path), m_Address(address), m_MaxRetry(maxRetry)
{
    auto tty = m_SerialInterface.getAttributes();

    // setup for non-canonical mode
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    // fetch bytes as they become available
    tty.c_cc[VMIN] = 1;  // set to 0 for non-blocking
    tty.c_cc[VTIME] = 5; // set timeout to 0.5 secs

    m_SerialInterface.setAttributes(tty);
}
//----------------------------------------------------------------------------
void RoboClaw::setMotor1Speed(float throttle)
{
    const bool forward = (throttle > 0.0f);
    const uint8_t byte = (uint8_t)std::round(127.0 * std::min(1.0f, forward ? throttle : -throttle));

    writeCommand(forward ? Command::M1FORWARD : Command::M1BACKWARD, byte);
}
//----------------------------------------------------------------------------
void RoboClaw::setMotor2Speed(float throttle)
{
    const bool forward = (throttle > 0.0f);
    const uint8_t byte = (uint8_t)std::round(127.0 * std::min(1.0f, forward ? throttle : -throttle));

    writeCommand(forward ? Command::M2FORWARD : Command::M2BACKWARD, byte);
}
//----------------------------------------------------------------------------
uint32_t RoboClaw::getMotor1Encoder()
{
    uint32_t encoder = 0;
    uint8_t status = 0;
    readCommand(Command::GETM1ENC, &encoder, &status);

    // **TODO** think about something sensible to do with status
    return encoder;
}
//----------------------------------------------------------------------------
uint32_t RoboClaw::getMotor2Encoder()
{
    uint32_t encoder = 0;
    uint8_t status = 0;
    readCommand(Command::GETM2ENC, &encoder, &status);

    // **TODO** think about something sensible to do with status
    return encoder;
}
//----------------------------------------------------------------------------
void RoboClaw::resetEncoders()
{
    writeCommand(Command::RESETENC);
}
//----------------------------------------------------------------------------
int RoboClaw::getMotor1Speed()
{
    uint32_t speed = 0;
    uint8_t status = 0;
    readCommand(Command::GETM1SPEED, &speed, &status);

    // **NOTE** status signifies direction but this also seems to be 2s compliment encoded in speed
    return (int)speed;
}
//----------------------------------------------------------------------------
int RoboClaw::getMotor2Speed()
{
    uint32_t speed = 0;
    uint8_t status = 0;
    readCommand(Command::GETM2SPEED, &speed, &status);

    // Cast to signed int and return
    // **NOTE** status signifies direction but this also seems to be 2s compliment encoded in speed
    return (int)speed;
}
//----------------------------------------------------------------------------
std::string RoboClaw::getVersion()
{
    // Retry
    for(int r = 0; r < m_MaxRetry; r++) {
        // Write address and command
        CRC crc;
        write(crc, m_Address, static_cast<uint8_t>(Command::GETVERSION));

        // Read maximum version characters
        uint8_t version[48];
        for(uint8_t &c : version) {
            m_SerialInterface.readByte(c);
            crc.update(c);

            // If end of string is reached
            if(c == '\0') {
                // Check CRC against one returned by device
                if(!crc.check(m_SerialInterface)) {
                    throw std::runtime_error("CRC check failed");
                }

                // Return string
                return reinterpret_cast<char*>(version);
            }
        }

        throw std::runtime_error("Version string not correctly terminated");
    }

    throw std::runtime_error("Unable to read to serial port");
}
//----------------------------------------------------------------------------
float RoboClaw::getBatteryVoltage()
{
    uint16_t voltage;
    readCommand(Command::GETMBATT, &voltage);
    return (voltage * 0.1f);
}
//----------------------------------------------------------------------------
RoboClaw::Status RoboClaw::getStatus()
{
    uint32_t status;
    readCommand(Command::GETSTATUS, &status);
    return static_cast<Status>(status);
}
}   // namespace BoBRobotics
#endif   // _WIN32
