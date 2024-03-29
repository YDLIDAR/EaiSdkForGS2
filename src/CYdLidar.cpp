/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2018, EAIBOT, Inc.
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/
#include "CYdLidar.h"
#include "common.h"
#include <map>
#include <angles.h>
#include <numeric>
//#include <iostream>

using namespace std;
using namespace ydlidar;
using namespace impl;
using namespace angles;


/*-------------------------------------------------------------
                        Constructor
-------------------------------------------------------------*/
CYdLidar::CYdLidar(): lidarPtr(nullptr) {
    m_SerialPort        = "";
    m_SerialBaudrate    = 230400;
    m_FixedResolution   = true;
    m_Reversion         = false;
    m_Inverted          = false;//
    m_AutoReconnect     = true;
    m_SingleChannel     = false;
    m_LidarType         = TYPE_TRIANGLE;
    m_MaxAngle          = 180.f;
    m_MinAngle          = -180.f;
    m_MaxRange          = 64.0;
    m_MinRange          = 0.01;
    m_SampleRate        = 5;
    defalutSampleRate   = 5;
    m_UserSampleRate    = 5;
    m_ScanFrequency     = 10;
    isScanning          = false;
    m_FixedSize         = 720;
    frequencyOffset     = 0.4;
    m_AbnormalCheckCount  = 4;
    Major               = 0;
    Minjor              = 0;
    m_IgnoreArray.clear();
    m_PointTime         = 1e9 / 5000;
    m_OffsetTime        = 0.0;
    m_AngleOffset       = 0.0;
    lidar_model = YDLIDAR_G2B;
    last_node_time = getTime();
    global_nodes = new node_info[YDlidarDriver::MAX_SCAN_NODES];
    m_ParseSuccess = false;
}

/*-------------------------------------------------------------
                    ~CYdLidar
-------------------------------------------------------------*/
CYdLidar::~CYdLidar() {
    disconnecting();

    if (global_nodes) {
        delete[] global_nodes;
        global_nodes = NULL;
    }
}

void CYdLidar::disconnecting() {
    if (lidarPtr) {
        lidarPtr->disconnect();
        delete lidarPtr;
        lidarPtr = nullptr;
    }

    isScanning = false;
}

//get zero angle offset value
float CYdLidar::getAngleOffset() const {
    return m_AngleOffset;
}

bool CYdLidar::isAngleOffetCorrected() const {
    return m_isAngleOffsetCorrected;
}

std::string CYdLidar::getSoftVersion() const {
    return m_lidarSoftVer;
}

std::string CYdLidar::getHardwareVersion() const {
    return m_lidarHardVer;
}

std::string CYdLidar::getSerialNumber() const {
    return m_lidarSerialNum;
}

bool CYdLidar::reset(uint8_t addr)
{
    if (!lidarPtr)
        return false;

    return (RESULT_OK == lidarPtr->reset(addr));
}

bool CYdLidar::isRangeValid(double reading) const {
    if (reading >= m_MinRange && reading <= m_MaxRange) {
        return true;
    }

    return false;
}

bool CYdLidar::isRangeIgnore(double angle) const {
    bool ret = false;

    for (uint16_t j = 0; j < m_IgnoreArray.size(); j = j + 2) {
        if ((angles::from_degrees(m_IgnoreArray[j]) <= angle) &&
                (angle <= angles::from_degrees(m_IgnoreArray[j + 1]))) {
            ret = true;
            break;
        }
    }

    return ret;
}


/*-------------------------------------------------------------
                        doProcessSimple
-------------------------------------------------------------*/
bool  CYdLidar::doProcessSimple(LaserScan &outscan,
                                bool &hardwareError) {
    hardwareError = false;

    // Bound?
    if (!checkHardware()) {
        hardwareError = true;
        delay(200 / m_ScanFrequency);
        return false;
    }

    size_t count = YDlidarDriver::MAX_SCAN_NODES;
    //wait Scan data:
    uint64_t tim_scan_start = getTime();
    uint64_t startTs = tim_scan_start;
    result_t op_result = lidarPtr->grabScanData(global_nodes, count);
    uint64_t tim_scan_end = getTime();

    int moduleNum = 0;
    // Fill in scan data:
    if (IS_OK(op_result))
    {
        outscan.moduleNum = global_nodes[0].index;
        moduleNum = outscan.moduleNum;
        if(moduleNum >= 3){
            moduleNum = 0;
        }
        int all_node_count = count;
        uint64_t scan_time  =   tim_scan_end - startTs;

        outscan.config.min_angle = angles::from_degrees(m_MinAngle);
        outscan.config.max_angle = angles::from_degrees(m_MaxAngle);
        outscan.config.scan_time = static_cast<float>(scan_time * 1.0 / 1e9);
        outscan.config.time_increment = outscan.config.scan_time / (double)(count - 1);
        outscan.config.min_range = m_MinRange;
        outscan.config.max_range = m_MaxRange;
        outscan.stamp = tim_scan_start;
        outscan.points.clear();

        if (m_FixedResolution) {
            all_node_count = m_FixedSize;
        }

        outscan.config.angle_increment = (outscan.config.max_angle -
                                          outscan.config.min_angle) / (all_node_count - 1);

        float range = 0.0;
        float intensity = 0.0;
        float angle = 0.0;

//        printf("points %lu\n", count);
        for (size_t i = 0; i < count; i++)
        {
            angle = static_cast<float>((global_nodes[i].angle_q6_checkbit >>
                                        LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) / 64.0f) + m_AngleOffset;
            range = static_cast<float>(global_nodes[i].distance_q2);

            intensity = static_cast<float>(global_nodes[i].sync_quality);
            angle = angles::from_degrees(angle);

            //Rotate 180 degrees or not
            if (m_Reversion) {
                angle = angle + M_PI;
            }

            //Is it counter clockwise
            if (m_Inverted) {
                angle = 2 * M_PI - angle;
            }

            angle = angles::normalize_angle(angle);
//            angle = angles::normalize_angle_positive(angle);

            //ignore angle
            if (isRangeIgnore(angle)) {
                range = 0.0;
            }

            //valid range
            if (!isRangeValid(range)) {
                range = 0.0;
                intensity = 0.0;
            }

//            printf("%lu %f %f\n", i, angles::to_degrees(angle), range);

            if (angle >= outscan.config.min_angle &&
                angle <= outscan.config.max_angle)
            {
                LaserPoint point;
                point.angle = angle;
                point.range = range;
                point.intensity = intensity;

                if (outscan.points.empty()) {
                    outscan.stamp = tim_scan_start + i * m_PointTime;
                }

                if (m_FixedResolution) {
                    int index = std::ceil((angle - outscan.config.min_angle) /
                                          outscan.config.angle_increment);

                    if (index >= 0 && index < all_node_count) {
                        outscan.points.push_back(point);
                    }
                } else {
                    outscan.points.push_back(point);
                }
            }
        }

        if (m_FixedResolution) {
            outscan.points.resize(all_node_count);
        }

        //   handleDeviceInfoPackage(count);

        return true;
    } else {
        if (IS_FAIL(op_result)) {
            // Error? Retry connection
        }
    }

    return false;

}

void CYdLidar::parsePackageNode(const node_info &node, LaserDebug &info) {
    switch (node.index) {
    case 0://W3F4CusMajor_W4F0CusMinor;
        info.W3F4CusMajor_W4F0CusMinor = node.debug_info[node.index];
        break;

    case 1://W4F3Model_W3F0DebugInfTranVer
        info.W4F3Model_W3F0DebugInfTranVer = node.debug_info[node.index];
        break;

    case 2://W3F4HardwareVer_W4F0FirewareMajor
        info.W3F4HardwareVer_W4F0FirewareMajor = node.debug_info[node.index];
        break;

    case 4://W3F4BoradHardVer_W4F0Moth
        info.W3F4BoradHardVer_W4F0Moth = node.debug_info[node.index];
        break;

    case 5://W2F5Output2K4K5K_W5F0Date
        info.W2F5Output2K4K5K_W5F0Date = node.debug_info[node.index];
        break;

    case 6://W1F6GNoise_W1F5SNoise_W1F4MotorCtl_W4F0SnYear
        info.W1F6GNoise_W1F5SNoise_W1F4MotorCtl_W4F0SnYear =
                node.debug_info[node.index];
        break;

    case 7://W7F0SnNumH
        info.W7F0SnNumH = node.debug_info[node.index];
        break;

    case 8://W7F0SnNumL
        info.W7F0SnNumL = node.debug_info[node.index];

        break;

    default:
        break;
    }

    if (node.index > info.MaxDebugIndex && node.index < 100) {
        info.MaxDebugIndex = static_cast<int>(node.index);
    }
}

void CYdLidar::handleDeviceInfoPackage(int count) {
    if (m_ParseSuccess) {
        return;
    }

    LaserDebug debug;
    debug.MaxDebugIndex = 0;

    for (int i = 0; i < count; i++) {
        parsePackageNode(global_nodes[i], debug);
    }

    device_info info;

    if (ParseLaserDebugInfo(debug, info)) {
        if (info.firmware_version != 0 ||
                info.hardware_version != 0) {
            std::string serial_number;

            for (int i = 0; i < 16; i++) {
                serial_number += std::to_string(info.serialnum[i] & 0xff);
            }

            Major = (uint8_t)(info.firmware_version >> 8);
            Minjor = (uint8_t)(info.firmware_version & 0xff);
            std::string softVer =  std::to_string(Major & 0xff) + "." + std::to_string(
                        Minjor & 0xff);
            std::string hardVer = std::to_string(info.hardware_version & 0xff);

            m_lidarSerialNum = serial_number;
            m_lidarSoftVer = softVer;
            m_lidarHardVer = hardVer;

            if (!m_ParseSuccess) {
                printfVersionInfo(info);
            }
        }

    }
}


/*-------------------------------------------------------------
                        turnOn
-------------------------------------------------------------*/
bool  CYdLidar::turnOn() {
    if (isScanning && lidarPtr->isscanning()) {
        return true;
    }

    // start scan...
    result_t op_result = lidarPtr->startScan();

    if (!IS_OK(op_result)) {
        op_result = lidarPtr->startScan();

        if (!IS_OK(op_result)) {
            lidarPtr->stop();
            fprintf(stderr, "[CYdLidar] Failed to start scan mode: %x\n", op_result);
            isScanning = false;
            return false;
        }
    }

    m_ParseSuccess &= !m_SingleChannel;
    m_PointTime = lidarPtr->getPointTime();

    //  if (checkLidarAbnormal()) {
    //    lidarPtr->stop();
    //    fprintf(stderr,
    //            "[CYdLidar] Failed to turn on the Lidar, because the lidar is blocked or the lidar hardware is faulty.\n");
    //    isScanning = false;
    //    return false;
    //  }

    //  if (m_SingleChannel && !m_ParseSuccess) {
    //    handleSingleChannelDevice();
    //  }

    m_PointTime = lidarPtr->getPointTime();
    isScanning = true;
    lidarPtr->setAutoReconnect(m_AutoReconnect);
    printf("[YDLIDAR INFO] Current Sampling Rate : %dK\n", m_SampleRate);
    printf("[YDLIDAR INFO] Now YDLIDAR is scanning ......\n");
    fflush(stdout);
    return true;
}

/*-------------------------------------------------------------
                        turnOff
-------------------------------------------------------------*/
bool  CYdLidar::turnOff() {
    if (lidarPtr) {
        lidarPtr->stop();
    }

    if (isScanning) {
        printf("[YDLIDAR INFO] Now YDLIDAR Scanning has stopped ......\n");
    }

    isScanning = false;
    return true;
}

/*-------------------------------------------------------------
            checkLidarAbnormal
-------------------------------------------------------------*/
bool CYdLidar::checkLidarAbnormal() {

    size_t   count = YDlidarDriver::MAX_SCAN_NODES;
    int check_abnormal_count = 0;

    if (m_AbnormalCheckCount < 2) {
        m_AbnormalCheckCount = 2;
    }

    result_t op_result = RESULT_FAIL;
    std::vector<int> data;
    int buffer_count  = 0;

    while (check_abnormal_count < m_AbnormalCheckCount) {
        //Ensure that the voltage is insufficient or the motor resistance is high, causing an abnormality.
        if (check_abnormal_count > 0) {
            delay(check_abnormal_count * 1000);
        }

        float scan_time = 0.0;
        uint32_t start_time = 0;
        uint32_t end_time = 0;
        op_result = RESULT_OK;

        while (buffer_count < 10 && (scan_time < 0.05 ||
                                     !lidarPtr->getSingleChannel()) && IS_OK(op_result)) {
            start_time = getms();
            count = YDlidarDriver::MAX_SCAN_NODES;
            op_result =  lidarPtr->grabScanData(global_nodes, count);
            end_time = getms();
            scan_time = 1.0 * static_cast<int32_t>(end_time - start_time) / 1e3;
            buffer_count++;

            if (IS_OK(op_result)) {
                //  handleDeviceInfoPackage(count);

                //if (CalculateSampleRate(count, scan_time)) {
                //    if (!lidarPtr->getSingleChannel()) {
                //        return !IS_OK(op_result);
                //    }
                //}
            }
        }

        if (IS_OK(op_result) && lidarPtr->getSingleChannel()) {
            data.push_back(count);
            int collection = 0;

            while (collection < 5) {
                count = YDlidarDriver::MAX_SCAN_NODES;
                start_time = getms();
                op_result =  lidarPtr->grabScanData(global_nodes, count);
                end_time = getms();


                if (IS_OK(op_result)) {
                    if (std::abs(static_cast<int>(data.front() - count)) > 10) {
                        data.erase(data.begin());
                    }

                    handleDeviceInfoPackage(count);
                    scan_time = 1.0 * static_cast<int32_t>(end_time - start_time) / 1e3;
                    data.push_back(count);

                    //if (CalculateSampleRate(count, scan_time)) {
					//
                    //}

                    if (scan_time > 0.05 && scan_time < 0.5 && lidarPtr->getSingleChannel()) {
                        m_SampleRate = static_cast<int>((count / scan_time + 500) / 1000);
                        m_PointTime = 1e9 / (m_SampleRate * 1000);
                        lidarPtr->setPointTime(m_PointTime);
                    }

                }

                collection++;
            }

            if (data.size() > 1) {
                int total = accumulate(data.begin(), data.end(), 0);
                int mean =  total / data.size(); //mean value
                m_FixedSize = (static_cast<int>((mean + 5) / 10)) * 10;
                printf("[YDLIDAR]:Fixed Size: %d\n", m_FixedSize);
                printf("[YDLIDAR]:Sample Rate: %dK\n", m_SampleRate);
                return false;
            }

        }

        check_abnormal_count++;
    }

    return !IS_OK(op_result);
}

void CYdLidar::handleSingleChannelDevice() {
    if (!lidarPtr || !lidarPtr->getSingleChannel()) {
        return;
    }

    return;
}

void CYdLidar::printfVersionInfo(const device_info &info) {
    if (info.firmware_version == 0 &&
            info.hardware_version == 0) {
        return;
    }

    m_ParseSuccess = true;
    lidar_model = info.model;
    Major = (uint8_t)(info.firmware_version >> 8);
    Minjor = (uint8_t)(info.firmware_version & 0xff);
    printf("[YDLIDAR] Connection established in [%s][%d]:\n"
           "Firmware version: %u.%u\n"
           "Hardware version: %u\n"
           "Model: %s\n"
           "Serial: ",
           m_SerialPort.c_str(),
           m_SerialBaudrate,
           Major,
           Minjor,
           (unsigned int)info.hardware_version,
           lidarModelToString(lidar_model).c_str());

    for (int i = 0; i < 16; i++) {
        printf("%01X", info.serialnum[i] & 0xff);
    }

    printf("\n");
}

/*-------------------------------------------------------------
                        checkCOMMs
-------------------------------------------------------------*/
bool  CYdLidar::checkCOMMs() {
    if (!lidarPtr) {
        printf("YDLidar SDK initializing\n");
        // create the driver instance
        lidarPtr = new YDlidarDriver();

        if (!lidarPtr) {
            fprintf(stderr, "Create Driver fail\n");
            return false;
        }

        printf("YDLidar SDK has been initialized\n");
        printf("[YDLIDAR]:SDK Version: %s\n", lidarPtr->getSDKVersion().c_str());
        fflush(stdout);
    }

    if (lidarPtr->isconnected()) {
        return true;
    }

    // Is it COMX, X>4? ->  "\\.\COMX"
    if (m_SerialPort.size() >= 3) {
        if (tolower(m_SerialPort[0]) == 'c' && tolower(m_SerialPort[1]) == 'o' &&
                tolower(m_SerialPort[2]) == 'm') {
            // Need to add "\\.\"?
            if (m_SerialPort.size() > 4 || m_SerialPort[3] > '4') {
                m_SerialPort = std::string("\\\\.\\") + m_SerialPort;
            }
        }
    }

    // make connection...
    result_t op_result = lidarPtr->connect(m_SerialPort.c_str(), m_SerialBaudrate);

    printf("[CYdLidar] connect to serial port[%s:%d]\n",
           m_SerialPort.c_str(), m_SerialBaudrate);

    if (!IS_OK(op_result)) {
        fprintf(stderr,
                "[CYdLidar] Error, cannot bind to the specified serial port[%s] and baudrate[%d]\n",
                m_SerialPort.c_str(), m_SerialBaudrate);
        return false;
    }

    printf("LiDAR successfully connected\n");
    lidarPtr->setSingleChannel(m_SingleChannel);
    lidarPtr->setLidarType(m_LidarType);
    lidarPtr->setIntensities(m_Intensity);

    return true;
}

/*-------------------------------------------------------------
                        checkStatus
-------------------------------------------------------------*/
bool CYdLidar::checkStatus() {

    if (!checkCOMMs()) {
        return false;
    }

    return true;
}

/*-------------------------------------------------------------
                        checkHardware
-------------------------------------------------------------*/
bool CYdLidar::checkHardware() {
    if (!lidarPtr) {
        return false;
    }

    if (isScanning && lidarPtr->isscanning()) {
        return true;
    }

    return false;
}

/*-------------------------------------------------------------
                        initialize
-------------------------------------------------------------*/
bool CYdLidar::initialize() {
    if (!checkCOMMs()) {
        fprintf(stderr,
                "[CYdLidar::initialize] Error initializing YDLIDAR check Comms.\n");
        fflush(stderr);
        return false;
    }

    //  if (!checkStatus()) {
    //    fprintf(stderr,
    //            "[CYdLidar::initialize] Error initializing YDLIDAR check status in port[%s] and baudrate[%d]\n", m_SerialPort.c_str(), m_SerialBaudrate);
    //    fflush(stderr);
    //    return false;
    //  }

    printf("LiDAR init success!\n");
    fflush(stdout);
    return true;
}
