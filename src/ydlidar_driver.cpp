﻿/*********************************************************************
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
#include "ydlidar_driver.h"
#include "common.h"
#include <math.h>
using namespace impl;

namespace ydlidar {

YDlidarDriver::YDlidarDriver():
    _serial(NULL) {
    isConnected         = false;
    isScanning          = false;
    //串口配置参数
    m_intensities       = false;
    isAutoReconnect     = true;
    isAutoconnting      = false;
    m_baudrate          = 230400;
    isSupportMotorDtrCtrl  = true;
    scan_node_count     = 0;
    sample_rate         = 5000;
    m_PointTime         = 1e9 / 5000;
    trans_delay         = 0;
    scan_frequence      = 0;
    m_sampling_rate     = -1;
    model               = -1;
    retryCount          = 0;
    has_device_header   = false;
    m_SingleChannel     = false;
    m_LidarType         = TYPE_TOF;

    //解析参数
    PackageSampleBytes  = 2;
    IntervalSampleAngle = 0.0;
    CheckSum            = 0;
    CheckSumCal         = 0;
    SampleNumlAndCTCal  = 0;
    LastSampleAngleCal  = 0;
    CheckSumResult      = true;
    Valu8Tou16          = 0;
    package_Sample_Num  = 0;
    moduleNum           = 0;
    frameNum            = 0;
    isPrepareToSend     = false;
    multi_package.resize(MaximumNumberOfPackages);

    last_device_byte    = 0x00;
    asyncRecvPos        = 0;
    async_size          = 0;
    headerBuffer = reinterpret_cast<uint8_t *>(&header_);
    infoBuffer = reinterpret_cast<uint8_t *>(&info_);
    healthBuffer = reinterpret_cast<uint8_t *>(&health_);
    get_device_health_success = false;
    get_device_info_success = false;

    package_Sample_Index = 0;
    IntervalSampleAngle_LastPackage = 0.0;
    globalRecvBuffer = new uint8_t[sizeof(gs2_node_package)];
    scan_node_buf = new node_info[MAX_SCAN_NODES];
    package_index = 0;
    has_package_error = false;
    isValidPoint  =  true;
    bias[0] = 0;
    bias[1] = 0;
    bias[2] = 0;
}

YDlidarDriver::~YDlidarDriver() {
    {
        isScanning = false;
    }

    isAutoReconnect = false;
    _thread.join();

    ScopedLocker lk(_serial_lock);

    if (_serial) {
        if (_serial->isOpen()) {
            _serial->flush();
            _serial->closePort();
        }
    }

    if (_serial) {
        delete _serial;
        _serial = NULL;
    }

    if (globalRecvBuffer) {
        delete[] globalRecvBuffer;
        globalRecvBuffer = NULL;
    }

    if (scan_node_buf) {
        delete[] scan_node_buf;
        scan_node_buf = NULL;
    }
}

result_t YDlidarDriver::connect(const char *port_path, uint32_t baudrate) {
    ScopedLocker lk(_serial_lock);
    m_baudrate = baudrate;
    serial_port = string(port_path);

    if (!_serial) {
        _serial = new serial::Serial(port_path, m_baudrate,
                                     serial::Timeout::simpleTimeout(DEFAULT_TIMEOUT));
    }

    {
        ScopedLocker l(_lock);

        if (!_serial->open()) {
            return RESULT_FAIL;
        }

        isConnected = true;

    }

    stopScan();
    delay(100);
    clearDTR();

    return RESULT_OK;
}


void YDlidarDriver::setDTR() {
    if (!isConnected) {
        return ;
    }

    if (_serial) {
        _serial->setDTR(1);
    }

}

void YDlidarDriver::clearDTR() {
    if (!isConnected) {
        return ;
    }

    if (_serial) {
        _serial->setDTR(0);
    }
}
void YDlidarDriver::flushSerial() {
    if (!isConnected) {
        return;
    }

    size_t len = _serial->available();

    if (len) {
        _serial->read(len);
    }

    delay(20);
}


void YDlidarDriver::disconnect() {
    isAutoReconnect = false;

    if (!isConnected) {
        return ;
    }

    stop();
    delay(10);
    ScopedLocker l(_serial_lock);

    if (_serial) {
        if (_serial->isOpen()) {
            _serial->closePort();
        }
    }

    isConnected = false;

}


void YDlidarDriver::disableDataGrabbing() {
    {
        if (isScanning) {
            isScanning = false;
            _dataEvent.set();
        }
    }
    _thread.join();
}

bool YDlidarDriver::isscanning() const {
    return isScanning;
}
bool YDlidarDriver::isconnected() const {
    return isConnected;
}

result_t YDlidarDriver::sendCommand(uint8_t cmd,
                                    const void *payload,
                                    size_t payloadsize)
{
    return sendCommand(0x00, cmd, payload, payloadsize);
//    uint8_t pkt_header[12];
//    cmd_packet_gs *header = reinterpret_cast<cmd_packet_gs * >(pkt_header);
//    uint8_t checksum = 0;

//    if (!isConnected) {
//        return RESULT_FAIL;
//    }

//    header->syncByte0 = LIDAR_CMD_SYNC_BYTE;
//    header->syncByte1 = LIDAR_CMD_SYNC_BYTE;
//    header->syncByte2 = LIDAR_CMD_SYNC_BYTE;
//    header->syncByte3 = LIDAR_CMD_SYNC_BYTE;
//    header->address = 0x00;
//    header->cmd_flag = cmd;
//    header->size = 0xffff&payloadsize;
//    sendData(pkt_header, 8) ;
//    checksum += cmd;
//    checksum += 0xff&header->size;
//    checksum += 0xff&(header->size>>8);
    
//    if (payloadsize && payload) {
//      for (size_t pos = 0; pos < payloadsize; ++pos) {
//        checksum += ((uint8_t *)payload)[pos];
//      }
//      uint8_t sizebyte = (uint8_t)(payloadsize);
//      sendData((const uint8_t *)payload, sizebyte);
//    }
    
//    sendData(&checksum, 1);

//    return RESULT_OK;
}

result_t YDlidarDriver::sendCommand(uint8_t addr,
                                    uint8_t cmd,
                                    const void *payload,
                                    size_t payloadsize)
{
    uint8_t pkt_header[12];
    cmd_packet_gs *header = reinterpret_cast<cmd_packet_gs * >(pkt_header);
    uint8_t checksum = 0;

    if (!isConnected) {
        return RESULT_FAIL;
    }

    header->syncByte0 = LIDAR_CMD_SYNC_BYTE;
    header->syncByte1 = LIDAR_CMD_SYNC_BYTE;
    header->syncByte2 = LIDAR_CMD_SYNC_BYTE;
    header->syncByte3 = LIDAR_CMD_SYNC_BYTE;
    header->address = addr;
    header->cmd_flag = cmd;
    header->size = 0xffff&payloadsize;
    sendData(pkt_header, 8) ;
    checksum += cmd;
    checksum += 0xff&header->size;
    checksum += 0xff&(header->size>>8);

    if (payloadsize && payload) {
      for (size_t pos = 0; pos < payloadsize; ++pos) {
        checksum += ((uint8_t *)payload)[pos];
      }
      uint8_t sizebyte = (uint8_t)(payloadsize);
      sendData((const uint8_t *)payload, sizebyte);
    }

    sendData(&checksum, 1);

    return RESULT_OK;
}

result_t YDlidarDriver::sendData(const uint8_t *data, size_t size) {
    if (!isConnected) {
        return RESULT_FAIL;
    }

    if (data == NULL || size == 0) {
        return RESULT_FAIL;
    }

    size_t r;

    while (size) {
        r = _serial->writeData(data, size);

        if (r < 1) {
            return RESULT_FAIL;
        }

        printf("send: ");
        printHex(data, r);

        size -= r;
        data += r;
    }

    return RESULT_OK;
}

result_t YDlidarDriver::getData(uint8_t *data, size_t size) {
    if (!isConnected) {
        return RESULT_FAIL;
    }

    size_t r;

    while (size) {
        r = _serial->readData(data, size);

        if (r < 1) {
            return RESULT_FAIL;
        }

//        printf("recv: ");
//        printHex(data, r);

        size -= r;
        data += r;
    }

    return RESULT_OK;
}

result_t YDlidarDriver::waitResponseHeader(gs_lidar_ans_header *header,
                                           uint32_t timeout) {
    int  recvPos     = 0;
    uint32_t startTs = getms();
    uint8_t  recvBuffer[sizeof(gs_lidar_ans_header)];
    uint8_t  *headerBuffer = reinterpret_cast<uint8_t *>(header);
    uint32_t waitTime = 0;
    has_device_header = false;
    last_device_byte = 0x00;
    
    while ((waitTime = getms() - startTs) <= timeout) {
      size_t remainSize = sizeof(gs_lidar_ans_header) - recvPos;
      size_t recvSize = 0;
      result_t ans = waitForData(remainSize, timeout - waitTime, &recvSize);
    
      if (!IS_OK(ans)) {
        return ans;
      }
    
      if (recvSize > remainSize) {
        recvSize = remainSize;
      }
    
      ans = getData(recvBuffer, recvSize);
    
      if (IS_FAIL(ans)) {
        return RESULT_FAIL;
      }
    
      for (size_t pos = 0; pos < recvSize; ++pos) {
        uint8_t currentByte = recvBuffer[pos];
    
        switch (recvPos) {
            case 0:
              if (currentByte != LIDAR_ANS_SYNC_BYTE1) {
                  recvPos = 0;
                  continue;
              }
              break;
    
            case 1:
              if (currentByte != LIDAR_ANS_SYNC_BYTE1) {
                  recvPos = 0;
                  continue;
              }
              break;
    
            case 2:
              if (currentByte != LIDAR_ANS_SYNC_BYTE1) {
                  recvPos = 0;
                  continue;
              }
              break;
    
            case 3:
              if (currentByte != LIDAR_ANS_SYNC_BYTE1) {
                  recvPos = 0;
                  continue;
              }
              has_device_header = true;
              break;
    
            default:
              break;
        }
    
        headerBuffer[recvPos++] = currentByte;
        last_device_byte = currentByte;
    
        if (has_device_header && recvPos == sizeof(gs_lidar_ans_header)) {
          return RESULT_OK;
        }
      }
    }

    return RESULT_FAIL;
}

result_t YDlidarDriver::waitForData(size_t data_count, uint32_t timeout,
                                    size_t *returned_size) {
    size_t length = 0;

    if (returned_size == NULL) {
        returned_size = (size_t *)&length;
    }

    return (result_t)_serial->waitfordata(data_count, timeout, returned_size);
}

result_t YDlidarDriver::checkAutoConnecting() {
    result_t ans = RESULT_FAIL;
    isAutoconnting = true;

    while (isAutoReconnect && isAutoconnting) {
        {
            ScopedLocker l(_serial_lock);

            if (_serial) {
                if (_serial->isOpen() || isConnected) {
                    isConnected = false;
                    _serial->closePort();
                    delete _serial;
                    _serial = NULL;
                }
            }
        }
        retryCount++;

        if (retryCount > 100) {
            retryCount = 100;
        }

        delay(100 * retryCount);
        int retryConnect = 0;

        while (isAutoReconnect &&
               connect(serial_port.c_str(), m_baudrate) != RESULT_OK) {
            retryConnect++;

            if (retryConnect > 25) {
                retryConnect = 25;
            }

            delay(200 * retryConnect);
        }

        if (!isAutoReconnect) {
            isScanning = false;
            return RESULT_FAIL;
        }

        if (isconnected()) {
            delay(100);
            {
                ScopedLocker l(_serial_lock);
                ans = startAutoScan();

                if (!IS_OK(ans)) {
                    ans = startAutoScan();
                }
            }

            if (IS_OK(ans)) {
                isAutoconnting = false;
                return ans;
            }
        }
    }

    return RESULT_FAIL;

}

int YDlidarDriver::cacheScanData() {
    node_info      local_buf[200];
    size_t         count = 200;
    node_info      local_scan[MAX_SCAN_NODES];
    size_t         scan_count = 0;
    result_t       ans = RESULT_FAIL;
    memset(local_scan, 0, sizeof(local_scan));

    flushSerial();
    waitScanData(local_buf, count);

    int timeout_count   = 0;
    retryCount = 0;

    while (isScanning)
    {
        count = 160;
        ans = waitScanData(local_buf, count);

        if (!IS_OK(ans)) {
            if (IS_FAIL(ans) || timeout_count > DEFAULT_TIMEOUT_COUNT) {
                if (!isAutoReconnect) {
                    fprintf(stderr, "exit scanning thread!!\n");
                    fflush(stderr);
                    {
                        isScanning = false;
                    }
                    return RESULT_FAIL;
                } else {
                    ans = checkAutoConnecting();

                    if (IS_OK(ans)) {
                        timeout_count = 0;
                        local_scan[0].sync_flag = Node_NotSync;
                    } else {
                        isScanning = false;
                        return RESULT_FAIL;
                    }
                }
            } else {
                timeout_count++;
                local_scan[0].sync_flag = Node_NotSync;
                fprintf(stderr, "timout count: %d\n", timeout_count);
                fflush(stderr);
            }
        } else {
            timeout_count = 0;
            retryCount = 0;
        }


        printf("sync:%d,index:%d,moduleNum:%d\n",package_type,frameNum,moduleNum);
        fflush(stdout);

        if(!isPrepareToSend){
            continue;
        }

        size_t size = multi_package.size();
        for(size_t i = 0;i < size; i++){
            if(multi_package[i].frameNum == frameNum && multi_package[i].moduleNum == moduleNum){
                memcpy(scan_node_buf,multi_package[i].all_points,sizeof (node_info) * 160);
                break;
            }
        }

        _lock.lock();//timeout lock, wait resource copys
        scan_node_buf[0].stamp = local_buf[count - 1].stamp;
        scan_node_buf[0].scan_frequence = local_buf[count - 1].scan_frequence;
        scan_node_buf[0].index = moduleNum >> 1;//gs2:  1, 2, 4
        scan_node_count = 160; //一个包固定160个数据
        printf("send frameNum: %d,moduleNum: %d\n",frameNum,moduleNum);
        fflush(stdout);
        _dataEvent.set();
        _lock.unlock();
        scan_count = 0;
        isPrepareToSend = false;
    }

    isScanning = false;

    return RESULT_OK;
}

result_t YDlidarDriver::waitPackage(node_info *node, uint32_t timeout)
{
    int recvPos         = 0;
    uint32_t startTs    = getms();
    uint32_t waitTime   = 0;
    uint8_t  *packageBuffer = (uint8_t *)&package;
    isValidPoint  =  true;
    int  package_recvPos    = 0;
    uint16_t sample_lens = 0;
    has_device_header = false;
    uint16_t package_Sample_Num = 0;

    (*node).index = 255;
    (*node).scan_frequence  = 0;

    if (package_Sample_Index == 0)
    {
        recvPos = 0;

        while ((waitTime = getms() - startTs) <= timeout)
        {
            size_t remainSize   = PackagePaidBytes_GS - recvPos;
            size_t recvSize     = 0;
            CheckSumCal = 0;
            result_t ans = waitForData(remainSize, timeout - waitTime, &recvSize);

            if (!IS_OK(ans)) {
                return ans;
            }

            if (recvSize > remainSize) {
                recvSize = remainSize;
            }

            getData(globalRecvBuffer, recvSize);

            for (size_t pos = 0; pos < recvSize; ++pos)
            {
                uint8_t currentByte = globalRecvBuffer[pos];
                switch (recvPos) {
                case 0:
                    if (currentByte != LIDAR_ANS_SYNC_BYTE1) {
                        recvPos = 0;
                        continue;
                    }
                    break;

                case 1:
                    if (currentByte != LIDAR_ANS_SYNC_BYTE1) {
                        recvPos = 0;
                        continue;
                    }
                    break;

                case 2:
                    if (currentByte != LIDAR_ANS_SYNC_BYTE1) {
                        recvPos = 0;
                        continue;
                    }
                    break;

                case 3:
                    if (currentByte != LIDAR_ANS_SYNC_BYTE1) {
                        recvPos = 0;
                        continue;
                    }
                    has_device_header = true;
                    break;

                case 4:
                    moduleNum = currentByte;
                    CheckSumCal += currentByte;
                    break;

                case 5:
                    if (currentByte != GS_LIDAR_ANS_SCAN) {
                        recvPos = 0;
                        CheckSumCal = 0;
                        moduleNum = 0;
                        has_device_header = false;
                        continue;
                    }
                    CheckSumCal += currentByte;
                    break;

                case 6:
                    sample_lens |= 0x00ff&currentByte;
                    CheckSumCal += currentByte;
                    break;

                case 7:
                    sample_lens |= (0x00ff&currentByte)<<8;
                    CheckSumCal += currentByte;
                    break;

                default :
                    break;
                }

                packageBuffer[recvPos++] = currentByte;
            }

            if (has_device_header &&
                recvPos  == PackagePaidBytes_GS) {
                package_Sample_Num = sample_lens+1;//环境2Bytes + 点云320Bytes + CRC
                package_recvPos = recvPos;
                printf("sample num %d\n", package_Sample_Num);
                break;
            }
            else {
                recvPos = 0;
                printf("invalid data\n");
                continue;
            }
        }

        if (PackagePaidBytes_GS == recvPos)
        {
            startTs = getms();
            recvPos = 0;

            while ((waitTime = getms() - startTs) <= timeout)
            {
                size_t remainSize = package_Sample_Num - recvPos;
                size_t recvSize = 0;
                result_t ans = waitForData(remainSize, timeout - waitTime, &recvSize);

                if (!IS_OK(ans)) {
                    return ans;
                }

                if (recvSize > remainSize) {
                    recvSize = remainSize;
                }

                getData(globalRecvBuffer, recvSize);

                for (size_t pos = 0; pos < recvSize-1; pos++) {
                    CheckSumCal += globalRecvBuffer[pos];
                    packageBuffer[package_recvPos + recvPos] = globalRecvBuffer[pos];
                    recvPos++;
                }
                CheckSum = globalRecvBuffer[recvSize-1];//crc
                packageBuffer[package_recvPos + recvPos] = CheckSum;//crc
                recvPos+=1;

                if (package_Sample_Num == recvPos) {
                    package_recvPos += recvPos;
                    break;
                }
            }

            if (package_Sample_Num != recvPos) {
                return RESULT_FAIL;
            }
        } else {
            return RESULT_FAIL;
        }

        if (CheckSumCal != CheckSum) {
            CheckSumResult = false;
            has_package_error = true;
        } else {
            CheckSumResult = true;
        }
    }

    if (!has_package_error) {
        if (package_Sample_Index == 0) {
            package_index++;
            (*node).index = package_index;
        }
    } else {
        (*node).index = 255;
        package_index = 0xff;
    }

    if (CheckSumResult) {
        (*node).index = package_index;
        (*node).scan_frequence  = scan_frequence;
    }

    (*node).sync_quality = Node_Default_Quality;
    (*node).stamp = 0;
    (*node).scan_frequence = 0;

    double sampleAngle = 0;
    if (CheckSumResult)
    {
        (*node).distance_q2 =
                package.packageSample[package_Sample_Index].PakageSampleDistance;

        if (m_intensities) {
            (*node).sync_quality = (uint16_t)package.packageSample[package_Sample_Index].PakageSampleQuality;
        }

        if (node->distance_q2 > 0)
        {
            angTransform((*node).distance_q2,package_Sample_Index,&sampleAngle,&(*node).distance_q2);
        }

//        printf("%lf ", sampleAngle);
        if (sampleAngle< 0) {
            (*node).angle_q6_checkbit = (((uint16_t)(sampleAngle * 64 + 23040)) << LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) +
                    LIDAR_RESP_MEASUREMENT_CHECKBIT;
        } else {
            if ((sampleAngle * 64) > 23040) {
                (*node).angle_q6_checkbit = (((uint16_t)(sampleAngle * 64 - 23040)) << LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) +
                        LIDAR_RESP_MEASUREMENT_CHECKBIT;
            } else {
                (*node).angle_q6_checkbit = (((uint16_t)(sampleAngle * 64)) << LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) +
                        LIDAR_RESP_MEASUREMENT_CHECKBIT;
            }
        }

        if(package_Sample_Index < 80){ //CT_RingStart  CT_Normal
            if((*node).angle_q6_checkbit <= 23041){
                (*node).distance_q2 = 0;
                isValidPoint = false;
            }
        }else {
            if((*node).angle_q6_checkbit > 23041){
                (*node).distance_q2 = 0;
                isValidPoint = false;
            }
        }

//        printf("%d(%d) ", node->distance_q2, package_Sample_Index);

    } else {
        (*node).sync_flag       = Node_NotSync;
        (*node).sync_quality    = Node_Default_Quality;
        (*node).angle_q6_checkbit = LIDAR_RESP_MEASUREMENT_CHECKBIT;
        (*node).distance_q2      = 0;
        (*node).scan_frequence  = 0;
    }

    uint8_t nowPackageNum = 160;

    package_Sample_Index++;
    (*node).sync_flag = Node_NotSync;

    if (package_Sample_Index >= nowPackageNum) {
        package_Sample_Index = 0;
        (*node).sync_flag = Node_Sync;
        CheckSumResult = false;
    }

    return RESULT_OK;
}

void YDlidarDriver::angTransform(uint16_t dist, int n, double *dstTheta, uint16_t *dstDist)
{
    double pixelU = n, Dist, theta, tempTheta, tempDist, tempX, tempY;
    uint8_t mdNum = 0x03 & (moduleNum >> 1);//1,2,4
    if (n < 80)
    {
      pixelU = 80 - pixelU;
      if (d_compensateB0[mdNum] > 1) {
          tempTheta = d_compensateK0[mdNum] * pixelU - d_compensateB0[mdNum];
      }
      else
      {
          tempTheta = atan(d_compensateK0[mdNum] * pixelU - d_compensateB0[mdNum]) * 180 / M_PI;
      }
      tempDist = (dist - Angle_Px) / cos(((Angle_PAngle + bias[mdNum]) - (tempTheta)) * M_PI / 180);
      tempTheta = tempTheta * M_PI / 180;
      tempX = cos((Angle_PAngle + bias[mdNum]) * M_PI / 180) * tempDist * cos(tempTheta) + sin((Angle_PAngle + bias[mdNum]) * M_PI / 180) * (tempDist *
                                                                                             sin(tempTheta));
      tempY = -sin((Angle_PAngle + bias[mdNum]) * M_PI / 180) * tempDist * cos(tempTheta) + cos((Angle_PAngle + bias[mdNum]) * M_PI / 180) * (tempDist *
                                                                                              sin(tempTheta));
      tempX = tempX + Angle_Px;
      tempY = tempY - Angle_Py; //5.315
      Dist = sqrt(tempX * tempX + tempY * tempY);
      theta = atan(tempY / tempX) * 180 / M_PI;
    }
    else
    {
      pixelU = 160 - pixelU;
      if (d_compensateB1[mdNum] > 1)
      {
          tempTheta = d_compensateK1[mdNum] * pixelU - d_compensateB1[mdNum];
      }
      else
      {
          tempTheta = atan(d_compensateK1[mdNum] * pixelU - d_compensateB1[mdNum]) * 180 / M_PI;
      }
      tempDist = (dist - Angle_Px) / cos(((Angle_PAngle + bias[mdNum]) + (tempTheta)) * M_PI / 180);
      tempTheta = tempTheta * M_PI / 180;
      tempX = cos(-(Angle_PAngle + bias[mdNum]) * M_PI / 180) * tempDist * cos(tempTheta) + sin(-(Angle_PAngle + bias[mdNum]) * M_PI / 180) * (tempDist *
                                                                                               sin(tempTheta));
      tempY = -sin(-(Angle_PAngle + bias[mdNum]) * M_PI / 180) * tempDist * cos(tempTheta) + cos(-(Angle_PAngle + bias[mdNum]) * M_PI / 180) * (tempDist *
                                                                                                sin(tempTheta));
      tempX = tempX + Angle_Px;
      tempY = tempY + Angle_Py; //5.315
      Dist = sqrt(tempX * tempX + tempY * tempY);
      theta = atan(tempY / tempX) * 180 / M_PI;
    }
    if (theta < 0)
    {
      theta += 360;
    }
    *dstTheta = theta;
    *dstDist = Dist;
}

void  YDlidarDriver::addPointsToVec(node_info *nodebuffer, size_t &count){
    size_t size = multi_package.size();
    bool isFound = false;
    for(size_t i =0;i < size; i++){
        if(multi_package[i].frameNum == frameNum && multi_package[i].moduleNum == moduleNum){
            isFound = true;
		    memcpy(multi_package[i].all_points,nodebuffer,sizeof (node_info) * count);
            isPrepareToSend = true;
            if(frameNum > 0){
                int lastFrame = frameNum - 1;
                for(size_t j =0;j < size; j++){
                    if(multi_package[j].frameNum == lastFrame && multi_package[j].moduleNum == moduleNum){
                        break;
                    }
                }
            }
            break;
        }
    }
    if(!isFound){
        GS2_Multi_Package  package;
        package.frameNum = frameNum;
        package.moduleNum = moduleNum;
        multi_package.push_back(package);
    }
    //   printf("add points, [sync:%d] [%u]\n",package_type,frameNum);
    //   fflush(stdout);
}

result_t YDlidarDriver::waitScanData(node_info *nodebuffer, size_t &count,
                                     uint32_t timeout) {
    if (!isConnected) {
        count = 0;
        return RESULT_FAIL;
    }

    size_t     recvNodeCount    =  0;
    uint32_t   startTs          = getms();
    uint32_t   waitTime         = 0;
    result_t   ans              = RESULT_FAIL;

    while ((waitTime = getms() - startTs) <= timeout && recvNodeCount < count)
    {
        node_info node;
        memset(&node, 0, sizeof(node_info));
        ans = waitPackage(&node, timeout - waitTime);

        if (!IS_OK(ans)) {
            count = recvNodeCount;
            return ans;
        }
        nodebuffer[recvNodeCount++] = node;
//        printf("%d ", node.distance_q2);

        if (!package_Sample_Index)
        {
//            printf("\n");

            size_t size = _serial->available();
            uint64_t delayTime = 0;
            size_t PackageSize = NORMAL_PACKAGE_SIZE;

            if (size > PackagePaidBytes_GS) {
                size_t packageNum = size / PackageSize;
                size_t Number = size % PackageSize;
                delayTime = packageNum * m_PointTime * PackageSize / 2;

                if (Number > PackagePaidBytes_GS) {
                    delayTime += m_PointTime * ((Number - PackagePaidBytes_GS) / 2);
                }

                size = Number;

                if (packageNum > 0 && Number == 0) {
                    size = PackageSize;
                }
            }
            addPointsToVec(nodebuffer,recvNodeCount);

            nodebuffer[recvNodeCount - 1].stamp = size * trans_delay + delayTime;
            nodebuffer[recvNodeCount - 1].scan_frequence = node.scan_frequence;
            count = recvNodeCount;
            return RESULT_OK;
        }

        if (recvNodeCount == count) {
            return RESULT_OK;
        }
    }

    count = recvNodeCount;
    return RESULT_FAIL;
}


result_t YDlidarDriver::grabScanData(node_info *nodebuffer, size_t &count,
                                     uint32_t timeout) {
    switch (_dataEvent.wait(timeout)) {
    case Event::EVENT_TIMEOUT:
        count = 0;
        return RESULT_TIMEOUT;

    case Event::EVENT_OK: {
        if (scan_node_count == 0) {
            return RESULT_FAIL;
        }

        ScopedLocker l(_lock);
        size_t size_to_copy = min(count, scan_node_count);
        memcpy(nodebuffer, scan_node_buf, size_to_copy * sizeof(node_info));
        count = size_to_copy;
        scan_node_count = 0;
    }

        return RESULT_OK;

    default:
        count = 0;
        return RESULT_FAIL;
    }

}


result_t YDlidarDriver::ascendScanData(node_info *nodebuffer, size_t count) {
    float inc_origin_angle = (float)360.0 / count;
    int i = 0;

    for (i = 0; i < (int)count; i++) {
        if (nodebuffer[i].distance_q2 == 0) {
            continue;
        } else {
            while (i != 0) {
                i--;
                float expect_angle = (nodebuffer[i + 1].angle_q6_checkbit >>
                                                                             LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) /
                        64.0f - inc_origin_angle;

                if (expect_angle < 0.0f) {
                    expect_angle = 0.0f;
                }

                uint16_t checkbit = nodebuffer[i].angle_q6_checkbit &
                        LIDAR_RESP_MEASUREMENT_CHECKBIT;
                nodebuffer[i].angle_q6_checkbit = (((uint16_t)(expect_angle * 64.0f)) <<
                                                   LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) + checkbit;
            }

            break;
        }
    }

    if (i == (int)count) {
        return RESULT_FAIL;
    }

    for (i = (int)count - 1; i >= 0; i--) {
        if (nodebuffer[i].distance_q2 == 0) {
            continue;
        } else {
            while (i != ((int)count - 1)) {
                i++;
                float expect_angle = (nodebuffer[i - 1].angle_q6_checkbit >>
                                                                             LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) /
                        64.0f + inc_origin_angle;

                if (expect_angle > 360.0f) {
                    expect_angle -= 360.0f;
                }

                uint16_t checkbit = nodebuffer[i].angle_q6_checkbit &
                        LIDAR_RESP_MEASUREMENT_CHECKBIT;
                nodebuffer[i].angle_q6_checkbit = (((uint16_t)(expect_angle * 64.0f)) <<
                                                   LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) + checkbit;
            }

            break;
        }
    }

    float frontAngle = (nodebuffer[0].angle_q6_checkbit >>
                                                           LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) / 64.0f;

    for (i = 1; i < (int)count; i++) {
        if (nodebuffer[i].distance_q2 == 0) {
            float expect_angle =  frontAngle + i * inc_origin_angle;

            if (expect_angle > 360.0f) {
                expect_angle -= 360.0f;
            }

            uint16_t checkbit = nodebuffer[i].angle_q6_checkbit &
                    LIDAR_RESP_MEASUREMENT_CHECKBIT;
            nodebuffer[i].angle_q6_checkbit = (((uint16_t)(expect_angle * 64.0f)) <<
                                               LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) + checkbit;
        }
    }

    size_t zero_pos = 0;
    float pre_degree = (nodebuffer[0].angle_q6_checkbit >>
                                                           LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) / 64.0f;

    for (i = 1; i < (int)count ; ++i) {
        float degree = (nodebuffer[i].angle_q6_checkbit >>
                        LIDAR_RESP_MEASUREMENT_ANGLE_SHIFT) / 64.0f;

        if (zero_pos == 0 && (pre_degree - degree > 180)) {
            zero_pos = i;
            break;
        }

        pre_degree = degree;
    }

    node_info *tmpbuffer = new node_info[count];

    for (i = (int)zero_pos; i < (int)count; i++) {
        tmpbuffer[i - zero_pos] = nodebuffer[i];
    }

    for (i = 0; i < (int)zero_pos; i++) {
        tmpbuffer[i + (int)count - zero_pos] = nodebuffer[i];
    }

    memcpy(nodebuffer, tmpbuffer, count * sizeof(node_info));
    delete[] tmpbuffer;

    return RESULT_OK;
}

/************************************************************************/
/* get device parameters of gs lidar                                             */
/************************************************************************/
result_t YDlidarDriver::getDevicePara(gs_device_para &info, uint32_t timeout) {
  result_t  ans;
  uint8_t crcSum, mdNum;
  uint8_t *pInfo = reinterpret_cast<uint8_t *>(&info);

  if (!isConnected) {
    return RESULT_FAIL;
  }

  disableDataGrabbing();
  flushSerial();
  {
    ScopedLocker l(_lock);

    if ((ans = sendCommand(GS_LIDAR_CMD_GET_PARAMETER)) != RESULT_OK) {
      return ans;
    }
    gs_lidar_ans_header response_header;
    for(int i = 0; i < PackageMaxModuleNums; i++)
    {
        if ((ans = waitResponseHeader(&response_header, timeout)) != RESULT_OK) {
          return ans;
        }
        if (response_header.type != GS_LIDAR_CMD_GET_PARAMETER) {
          return RESULT_FAIL;
        }
        if (response_header.size < (sizeof(gs_device_para) - 1)) {
          return RESULT_FAIL;
        }
        if (waitForData(response_header.size+1, timeout) != RESULT_OK) {
          return RESULT_FAIL;
        }
        getData(reinterpret_cast<uint8_t *>(&info), sizeof(info));
        
        crcSum = 0;
        crcSum += response_header.address;
        crcSum += response_header.type;
        crcSum += 0xff & response_header.size;
        crcSum += 0xff & (response_header.size >> 8);
        for(int j = 0; j < response_header.size; j++) {
            crcSum += pInfo[j];
        }
        if(crcSum != info.crc) {
            return RESULT_FAIL;
        }

        mdNum = response_header.address >> 1; // 1,2,4
        if( mdNum > 2) {
            return RESULT_FAIL;
        }
        u_compensateK0[mdNum] = info.u_compensateK0;
        u_compensateK1[mdNum] = info.u_compensateK1;
        u_compensateB0[mdNum] = info.u_compensateB0;
        u_compensateB1[mdNum] = info.u_compensateB1;
        d_compensateK0[mdNum] = info.u_compensateK0 / 10000.00;
        d_compensateK1[mdNum] = info.u_compensateK1 / 10000.00;
        d_compensateB0[mdNum] = info.u_compensateB0 / 10000.00;
        d_compensateB1[mdNum] = info.u_compensateB1 / 10000.00;
        bias[mdNum] = double(info.bias) * 0.1;
        delay(5);
    }
  }

  return RESULT_OK;
}

result_t YDlidarDriver::setDeviceAddress(uint32_t timeout)
{
    result_t ans;

    if (!isConnected) {
        return RESULT_FAIL;
    }

    if (m_SingleChannel) {
        return RESULT_OK;
    }

    disableDataGrabbing();
    flushSerial();
    {
        ScopedLocker l(_lock);

        if ((ans = sendCommand(GS_LIDAR_CMD_GET_ADDRESS)) != RESULT_OK) {
            return ans;
        }

        gs_lidar_ans_header response_header;
        if ((ans = waitResponseHeader(&response_header, timeout)) != RESULT_OK) {
            return ans;
        }

        if (response_header.type != GS_LIDAR_CMD_GET_ADDRESS) {
            return RESULT_FAIL;
        }

        printf("[YDLIDAR] Lidar module count %d", (response_header.address << 1) + 1);
    }

    return RESULT_OK;
}

/************************************************************************/
/* the set to signal quality                                            */
/************************************************************************/
void YDlidarDriver::setIntensities(const bool &isintensities) {
    if (m_intensities != isintensities) {
        if (globalRecvBuffer) {
            delete[] globalRecvBuffer;
            globalRecvBuffer = NULL;
        }

        globalRecvBuffer = new uint8_t[sizeof(gs2_node_package)];
    }

    m_intensities = isintensities;

    if (m_intensities) {
        PackageSampleBytes = 2;
    } else {
        PackageSampleBytes = 2;
    }
}
/**
* @brief 设置雷达异常自动重新连接 \n
* @param[in] enable    是否开启自动重连:
*     true	开启
*	  false 关闭
*/
void YDlidarDriver::setAutoReconnect(const bool &enable) {
    isAutoReconnect = enable;
}

void YDlidarDriver::checkTransDelay() {
    //calc stamp
    trans_delay = _serial->getByteTime();
    sample_rate = lidarModelDefaultSampleRate(model) * 1000;

    switch (model) {
    case YDLIDAR_G4://g4
    case YDLIDAR_G5:
    case YDLIDAR_G4PRO:
    case YDLIDAR_F4PRO:
    case YDLIDAR_G6://g6
    case YDLIDAR_G7:
    case YDLIDAR_TG15:
    case YDLIDAR_TG30:
    case YDLIDAR_TG50:
        if (m_sampling_rate == -1) {
            sampling_rate _rate;
            _rate.rate = 0;
            //getSamplingRate(_rate);
            m_sampling_rate = _rate.rate;
        }

        sample_rate = ConvertLidarToUserSmaple(model, m_sampling_rate);
        sample_rate *= 1000;

        break;

    case YDLIDAR_G2C:
        sample_rate = 4000;
        break;

    case YDLIDAR_G1:
        sample_rate = 9000;
        break;

    case YDLIDAR_G4C:
        sample_rate = 4000;
        break;

    default:
        break;
    }

    m_PointTime = 1e9 / sample_rate;
}

/************************************************************************/
/*  start to scan                                                       */
/************************************************************************/
result_t YDlidarDriver::startScan(bool force, uint32_t timeout) {
    result_t ans;

    if (!isConnected) {
        return RESULT_FAIL;
    }

    if (isScanning) {
        return RESULT_OK;
    }

    stop();
    checkTransDelay();
    flushSerial();

    //配置GS2模组地址（三个模组）
    setDeviceAddress(300);

    //获取GS2参数
    gs_device_para gs2_info;
//    delay(30);
    getDevicePara(gs2_info, 300);  
//    delay(30);
    {
        flushSerial();

        ScopedLocker l(_lock);
        if ((ans = sendCommand(force ? LIDAR_CMD_FORCE_SCAN : GS_LIDAR_CMD_SCAN)) !=
                RESULT_OK) {
            return ans;
        }

        if (!m_SingleChannel)
        {
            gs_lidar_ans_header response_header;

            if ((ans = waitResponseHeader(&response_header, timeout)) != RESULT_OK) {
                return ans;
            }

            if (response_header.type != GS_LIDAR_ANS_SCAN) {
                printf("[CYdLidar] Response to start scan type error!\n");
                return RESULT_FAIL;
            }
        }

        ans = this->createThread();
    }

    return ans;
}


result_t YDlidarDriver::stopScan(uint32_t timeout) {
    UNUSED(timeout);
    result_t  ans;

    if (!isConnected) {
        return RESULT_FAIL;
    }

    ScopedLocker l(_lock);
    
    if ((ans = sendCommand(GS_LIDAR_CMD_STOP)) != RESULT_OK) {
      return ans;
    }
    gs_lidar_ans_header response_header;
    if ((ans = waitResponseHeader(&response_header, timeout)) != RESULT_OK) {
        return ans;
    }
    if (response_header.type != GS_LIDAR_CMD_STOP) {
        return RESULT_FAIL;
    }    
    delay(10);

    return RESULT_OK;
}

result_t YDlidarDriver::createThread() {
    _thread = CLASS_THREAD(YDlidarDriver, cacheScanData);

    if (_thread.getHandle() == 0) {
        isScanning = false;
        return RESULT_FAIL;
    }

    isScanning = true;
    return RESULT_OK;
}


result_t YDlidarDriver::startAutoScan(bool force, uint32_t timeout) {
    result_t ans;

    if (!isConnected) {
        return RESULT_FAIL;
    }

    flushSerial();
    delay(10);
    {

        ScopedLocker l(_lock);

        if ((ans = sendCommand(force ? LIDAR_CMD_FORCE_SCAN : GS_LIDAR_CMD_SCAN)) !=
                RESULT_OK) {
            return ans;
        }

        if (!m_SingleChannel) {
            gs_lidar_ans_header response_header;

            if ((ans = waitResponseHeader(&response_header, timeout)) != RESULT_OK) {
                return ans;
            }

            if (response_header.type != GS_LIDAR_CMD_SCAN) {
                return RESULT_FAIL;
            }
        }

    }

    return RESULT_OK;
}

/************************************************************************/
/*   stop scan                                                   */
/************************************************************************/
result_t YDlidarDriver::stop() {
    if (isAutoconnting) {
        isAutoReconnect = false;
        isScanning = false;
    }

    disableDataGrabbing();
    stopScan();

    return RESULT_OK;
}

/************************************************************************/
/*  reset device                                                        */
/************************************************************************/
result_t YDlidarDriver::reset(uint8_t addr, uint32_t timeout) {
    UNUSED(timeout);
    result_t ans;

    if (!isConnected) {
        return RESULT_FAIL;
    }

    ScopedLocker l(_lock);

    if ((ans = sendCommand(addr, GS_LIDAR_CMD_RESET)) != RESULT_OK) {
        return ans;
    }

    return RESULT_OK;
}

std::string YDlidarDriver::getSDKVersion() {
    return SDKVerision;
}

std::map<std::string, std::string> YDlidarDriver::lidarPortList() {
    std::vector<PortInfo> lst = list_ports();
    std::map<std::string, std::string> ports;

    for (std::vector<PortInfo>::iterator it = lst.begin(); it != lst.end(); it++) {
        std::string port = "ydlidar" + (*it).device_id;
        ports[port] = (*it).port;
    }

    return ports;
}

void YDlidarDriver::printHex(const uint8_t *data, int size)
{
    if (!data)
        return;
    for (int i=0; i<size; ++i)
        printf("%02X", data[i]);
    printf("\n");
}

}
