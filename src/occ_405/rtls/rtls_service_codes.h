/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/occ_405/rtls/rtls_service_codes.h $                       */
/*                                                                        */
/* OpenPOWER OnChipController Project                                     */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2011,2015                        */
/* [+] International Business Machines Corp.                              */
/*                                                                        */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */

#ifndef _RTLS_SERVICE_CODES_H_
#define _RTLS_SERVICE_CODES_H_

#include <comp_ids.h>

enum rtlsModuleId
{
    RTLS_OCB_INIT_MOD                 = RTLS_COMP_ID | 0x00,
    RTLS_DO_TICK_MOD                  = RTLS_COMP_ID | 0x01,
    RTLS_START_TASK_MOD               = RTLS_COMP_ID | 0x02,
    RTLS_STOP_TASK_MOD                = RTLS_COMP_ID | 0x03,
    RTLS_TASK_RUNABLE_MOD             = RTLS_COMP_ID | 0x04,
    RTLS_SET_TASK_DATA_MOD            = RTLS_COMP_ID | 0x05,
    RTLS_TASK_CORE_DATA_CONTROL_MOD   = RTLS_COMP_ID | 0x06,
};

#endif /* #ifndef _RTLS_SERVICE_CODES_H_ */
