/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/occ_405/amec/amec_health.c $                              */
/*                                                                        */
/* OpenPOWER OnChipController Project                                     */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2011,2019                        */
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

//*************************************************************************/
// Includes
//*************************************************************************/
#include "amec_health.h"
#include "amec_sys.h"
#include "amec_service_codes.h"
#include "occ_service_codes.h"
#include <centaur_data.h>
#include <proc_data.h>

//*************************************************************************/
// Externs
//*************************************************************************/
extern bool G_simics_environment;
extern bool G_log_gpe1_error;

//*************************************************************************/
// Defines/Enums
//*************************************************************************/

//*************************************************************************/
// Globals
//*************************************************************************/

// Have we already called out the dimm for overtemp (bitmap of dimms)?
dimm_sensor_flags_t G_dimm_overtemp_logged_bitmap = {{0}};

// Have we already called out the dimm for timeout (bitmap of dimms)?
dimm_sensor_flags_t G_dimm_timeout_logged_bitmap = {{0}};

// Are any dimms currently in the timedout state (bitmap of dimm)?
dimm_sensor_flags_t G_dimm_temp_expired_bitmap = {{0}};

// Timedout state of OCMB "DIMM" sensors by fru type (bitmap of DTS type)
uint8_t G_ocm_dts_type_expired_bitmap = 0;

// Have we already called out the centaur for timeout (bitmap of centaurs)?
uint16_t G_cent_timeout_logged_bitmap = 0;

// Have we already called out the centaur for overtemp (bitmap of centaurs)?
uint16_t G_cent_overtemp_logged_bitmap = 0;

// Are any mem controllers currently in the timedout state (bitmap of centaurs)?
uint16_t G_cent_temp_expired_bitmap = 0;

// Array to store the update tag of each core's temperature sensor
uint32_t G_core_temp_update_tag[MAX_NUM_CORES] = {0};

// Reading VRM Vdd temperature timedout?
bool G_vrm_vdd_temp_expired = false;

//*************************************************************************/
// Function Declarations
//*************************************************************************/

//*************************************************************************/
// Functions
//*************************************************************************/
uint64_t amec_mem_get_huid(uint8_t i_cent, uint8_t i_dimm)
{
    uint64_t l_huid;

    if(i_dimm == 0xff)
    {
        //we're being asked for a centaur huid
        l_huid = G_sysConfigData.centaur_huids[i_cent];
    }
    else
    {
        //we're being asked for a dimm huid
        l_huid = G_sysConfigData.dimm_huids[i_cent][i_dimm];
        if(l_huid == 0)
        {
            if (MEM_TYPE_CUMULUS == G_sysConfigData.mem_type)
            {
                //if we don't have a valid dimm huid, use the centaur huid.
                l_huid = G_sysConfigData.centaur_huids[i_cent];
            }
            else
            {
                // else NIMBUS huid of 0 indicates not present (should never get called)
                TRAC_ERR("amec_mem_get_huid: DIMM%04X did not have a HUID to call out!", (i_cent<<8)|i_dimm);
            }
        }
    }
    return l_huid;
}

//If i_dimm is 0xff it is assumed that the caller wishes to
//mark the centaur as being logged.  Otherwise, it is assumed
//that the dimm should be marked.
void amec_mem_mark_logged(uint8_t i_cent,
                          uint8_t i_dimm,
                          uint16_t* i_clog_bitmap,
                          uint8_t*  i_dlog_bitmap)
{
    if(i_dimm == 0xff)
    {
        //mark the centaur as being called out.
        *i_clog_bitmap |= CENTAUR0_PRESENT_MASK >> i_cent;
    }
    else
    {
        //mark the dimm as being called out.
        *i_dlog_bitmap |= DIMM_SENSOR0 >> i_dimm;
    }
}


/*
 * Function Specification
 *
 * Name: amec_health_check_dimm_temp
 *
 * Description: Check if DIMM temperature exceeds the error temperature
 *              as defined in thermal control thresholds
 *              (ERROR field for DIMM FRU Type)
 *
 * End Function Specification
 */
void amec_health_check_dimm_temp()
{
    uint16_t                    l_ot_error, l_max_temp;
    sensor_t                    *l_sensor;
    uint8_t                     l_dimm;
    uint8_t                     l_port;
    uint8_t                     l_max_port; // #ports in nimbus/#mem buf in cumulus/OCM
    uint8_t                     l_max_dimm_per_port; // per port in nimbus/per mem buf in cumulus/OCM
    uint32_t                    l_callouts_count = 0;
    uint8_t                     l_new_callouts;
    uint64_t                    l_huid;
    errlHndl_t                  l_err = NULL;

    if(G_sysConfigData.mem_type == MEM_TYPE_NIMBUS)
    {
        l_max_port = NUM_DIMM_PORTS;
        l_max_dimm_per_port = NUM_DIMMS_PER_I2CPORT;
    }
    else if(G_sysConfigData.mem_type == MEM_TYPE_OCM)
    {
        l_max_port = MAX_NUM_OCMBS;
        l_max_dimm_per_port = NUM_DIMMS_PER_OCMB;
    }
    else // MEM_TYPE_CUMULUS
    {
        l_max_port = MAX_NUM_CENTAURS;
        l_max_dimm_per_port = NUM_DIMMS_PER_CENTAUR;
    }

    // Check to see if any dimms have reached the error temperature that
    // haven't been called out already
    if( (G_dimm_overtemp_bitmap.dw[0] == G_dimm_overtemp_logged_bitmap.dw[0]) &&
        (G_dimm_overtemp_bitmap.dw[1] == G_dimm_overtemp_logged_bitmap.dw[1]) )
    {
        return;
    }

    //iterate over all dimms
    for(l_port = 0; l_port < l_max_port; l_port++)
    {
        //only callout a dimm if it hasn't been called out already
        l_new_callouts = G_dimm_overtemp_bitmap.bytes[l_port] ^
                         G_dimm_overtemp_logged_bitmap.bytes[l_port];

        //skip to next port if no new callouts for this one
        if (!l_new_callouts || (G_dimm_overtemp_bitmap.bytes[l_port] == 0))
        {
            continue;
        }

        // if the previous port had errors commit it so this port gets new error log
        if(l_err)
        {
           commitErrl(&l_err);
           l_callouts_count = 0;
        }

        //find the dimm(s) that need to be called out for this port
        for(l_dimm = 0; l_dimm < l_max_dimm_per_port; l_dimm++)
        {
            if (!(l_new_callouts & (DIMM_SENSOR0 >> l_dimm)))
            {
                continue;
            }

            fru_temp_t* l_fru;
            l_fru = &g_amec->proc[0].memctl[l_port].centaur.dimm_temps[l_dimm];
            switch(l_fru->temp_fru_type)
            {
               case DATA_FRU_DIMM:
                  l_ot_error = g_amec->thermaldimm.ot_error;
                  l_sensor = getSensorByGsid(TEMPDIMMTHRM);
                  l_max_temp = l_sensor->sample_max;
                  break;

               case DATA_FRU_MEMCTRL_DRAM:
                  l_ot_error = g_amec->thermalmcdimm.ot_error;
                  l_sensor = getSensorByGsid(TEMPMCDIMMTHRM);
                  l_max_temp = l_sensor->sample_max;
                  break;

               case DATA_FRU_PMIC:
                  l_ot_error = g_amec->thermalpmic.ot_error;
                  l_sensor = getSensorByGsid(TEMPPMICTHRM);
                  l_max_temp = l_sensor->sample_max;
                  break;

               case DATA_FRU_MEMCTRL_EXT:
                  l_ot_error = g_amec->thermalmcext.ot_error;
                  l_sensor = getSensorByGsid(TEMPMCEXTTHRM);
                  l_max_temp = l_sensor->sample_max;
                  break;

               default:
                  // this is a code bug trace and let the error be logged for debug
                  TRAC_ERR("amec_health_check_dimm_temp: sensor[%04X] marked as OT has invalid type[%d]",
                     (l_port<<8)|l_dimm, l_fru->temp_fru_type);
                  l_ot_error = 0xff;
                  l_max_temp = 0xff;
                  break;
            }
            TRAC_ERR("amec_health_check_dimm_temp: sensor[%04X] type[0x%02X] reached error temp[%d] current[%d]",
                     (l_port<<8)|l_dimm, l_fru->temp_fru_type, l_ot_error, l_fru->cur_temp);

            amec_mem_mark_logged(l_port,
                                 l_dimm,
                                 &G_cent_overtemp_logged_bitmap,
                                 &G_dimm_overtemp_logged_bitmap.bytes[l_port]);

            // Create single elog with up to MAX_CALLOUTS
            // this will be generic regardless of temperature sensor type, the callouts will be correct
            // and the traces will point to specific types/thresholds
            if(l_callouts_count < ERRL_MAX_CALLOUTS)
            {
                //If we don't have an error log for the callout, create one
                if(!l_err)
                {
                    TRAC_ERR("amec_health_check_dimm_temp: Creating log for port[%d] OT bitmap[0x%02X] logged bitmap[0x%02X]",
                             l_port,
                             G_dimm_overtemp_bitmap.bytes[l_port],
                             G_dimm_overtemp_logged_bitmap.bytes[l_port]);
                    /* @
                     * @errortype
                     * @moduleid    AMEC_HEALTH_CHECK_DIMM_TEMP
                     * @reasoncode  DIMM_ERROR_TEMP
                     * @userdata1   Maximum DIMM temperature
                     * @userdata2   DIMM temperature threshold
                     * @userdata4   OCC_NO_EXTENDED_RC
                     * @devdesc     Memory DIMM(s) exceeded maximum safe
                     *              temperature.
                     */
                    l_err = createErrl(AMEC_HEALTH_CHECK_DIMM_TEMP,    //modId
                                       DIMM_ERROR_TEMP,               //reasoncode
                                       OCC_NO_EXTENDED_RC,            //Extended reason code
                                       ERRL_SEV_PREDICTIVE,           //Severity
                                       NULL,                          //Trace Buf
                                       DEFAULT_TRACE_SIZE,            //Trace Size
                                       l_max_temp,                    //userdata1
                                       l_ot_error);                   //userdata2

                    // Callout the "over temperature" procedure
                    addCalloutToErrl(l_err,
                                     ERRL_CALLOUT_TYPE_COMPONENT_ID,
                                     ERRL_COMPONENT_ID_OVER_TEMPERATURE,
                                     ERRL_CALLOUT_PRIORITY_HIGH);
                    l_callouts_count = 1;
                }

                // Callout dimm
                l_huid = amec_mem_get_huid(l_port, l_dimm);
                addCalloutToErrl(l_err,
                                 ERRL_CALLOUT_TYPE_HUID,
                                 l_huid,
                                 ERRL_CALLOUT_PRIORITY_MED);

                l_callouts_count++;
            }
        }//iterate over dimms
    }//iterate over ports

    if(l_err)
    {
        commitErrl(&l_err);
    }

} // end amec_health_check_dimm_temp()


/*
 * Function Specification
 *
 * Name: amec_health_check_dimm_timeout
 *
 * Description: Check for centaur-dimm/rdimm-modules timeout condition
 *              as defined in thermal control thresholds
 *              (MAX_READ_TIMEOUT field for Centaur/DIMM FRU Type)
 *
 * End Function Specification
 */
void amec_health_check_dimm_timeout()
{
    static dimm_sensor_flags_t L_temp_update_bitmap_prev = {{0}};
    dimm_sensor_flags_t l_need_inc, l_need_clr, l_temp_update_bitmap;
    uint8_t l_dimm, l_port, l_temp_timeout;
    fru_temp_t* l_fru;
    errlHndl_t  l_err = NULL;
    uint32_t    l_callouts_count = 0;
    uint64_t    l_huid;
    static bool L_ran_once = FALSE;
    uint8_t     l_max_port = 0; // #ports in nimbus/#mem buffs in cumulus/OCM
    uint8_t     l_max_dimm_per_port = 0; // per port in nimbus/per mem buff in cumulus/OCM
    uint8_t     l_ocm_dts_type_expired_bitmap = 0;

    do
    {
        //For every dimm sensor there are 3 cases to consider
        //
        //1) sensor is enabled and not updated (need to increment timer and check for timeout)
        //2) sensor is enabled and updated but wasn't updated on previous check (need to clear timer)
        //3) sensor is enabled and updated and was updated on previous check (do nothing)

        //Grab snapshot of G_dimm_temp_updated_bitmap and clear it
        l_temp_update_bitmap.dw[0] = G_dimm_temp_updated_bitmap.dw[0];
        l_temp_update_bitmap.dw[1] = G_dimm_temp_updated_bitmap.dw[1];
        G_dimm_temp_updated_bitmap.dw[0] = 0;
        G_dimm_temp_updated_bitmap.dw[1] = 0;

        //check if we need to increment any timers (haven't been updated in the last second)
        l_need_inc.dw[0] = G_dimm_enabled_sensors.dw[0] & ~l_temp_update_bitmap.dw[0];
        l_need_inc.dw[1] = G_dimm_enabled_sensors.dw[1] & ~l_temp_update_bitmap.dw[1];

        //check if we need to clear any timers (updated now but not updated previously)
        l_need_clr.dw[0] = l_temp_update_bitmap.dw[0] & ~L_temp_update_bitmap_prev.dw[0];
        l_need_clr.dw[1] = l_temp_update_bitmap.dw[1] & ~L_temp_update_bitmap_prev.dw[1];

        //save off the previous bitmap of updated sensors for next time
        L_temp_update_bitmap_prev.dw[0] = l_temp_update_bitmap.dw[0];
        L_temp_update_bitmap_prev.dw[1] = l_temp_update_bitmap.dw[1];

        //only go further if we actually have work to do here.
        if(!l_need_inc.dw[0] && !l_need_inc.dw[1] &&
           !l_need_clr.dw[0] && !l_need_clr.dw[1])
        {
            //nothing to do
            break;
        }

        if(G_sysConfigData.mem_type == MEM_TYPE_NIMBUS)
        {
            l_max_port = NUM_DIMM_PORTS;
            l_max_dimm_per_port = NUM_DIMMS_PER_I2CPORT;
        }
        else if(G_sysConfigData.mem_type == MEM_TYPE_OCM)
        {
            l_max_port = MAX_NUM_OCMBS;
            l_max_dimm_per_port = NUM_DIMMS_PER_OCMB;
        }
        else // MEM_TYPE_CUMULUS
        {
            l_max_port = MAX_NUM_CENTAURS;
            l_max_dimm_per_port = NUM_DIMMS_PER_CENTAUR;
        }
        //iterate across all ports incrementing dimm sensor timers as needed
        for(l_port = 0; l_port < l_max_port; l_port++)
        {
            //any dimm timers on this port need incrementing?
            if(!l_need_inc.bytes[l_port])
            {
                // All dimm sensors were updated for this port
                // Trace this fact and clear the expired byte for all DIMMs on this port
                if(G_dimm_temp_expired_bitmap.bytes[l_port])
                {
                    G_dimm_temp_expired_bitmap.bytes[l_port] = 0;
                    TRAC_INFO("All DIMM sensors for port %d have been updated", l_port);
                }
                continue;
            }

            //There's at least one dimm requiring an increment, find the dimm
            for(l_dimm = 0; l_dimm < l_max_dimm_per_port; l_dimm++)
            {
                //not this one, check if we need to clear the dimm timeout and go to the next one
                if(!(l_need_inc.bytes[l_port] & (DIMM_SENSOR0 >> l_dimm)))
                {
                    // Clear this one if needed
                    if(G_dimm_temp_expired_bitmap.bytes[l_port] & (DIMM_SENSOR0 >> l_dimm))
                    {
                        G_dimm_temp_expired_bitmap.bytes[l_port] &= ~(DIMM_SENSOR0 >> l_dimm);
                    }
                    continue;
                }

                //we found one.
                l_fru = &g_amec->proc[0].memctl[l_port].centaur.dimm_temps[l_dimm];

                //increment timer
                l_fru->sample_age++;

                //handle wrapping
                if(!l_fru->sample_age)
                {
                    l_fru->sample_age = -1;
                }

                // In Simics: the RTL timer is increased and a DIMM reading will not always
                // complete on each call.  (an error will still be logged if reading does not
                // meet the DIMM MAX_READ_TIMEOUT.)
                if((l_fru->sample_age == 1) && (!G_simics_environment))
                {
                    TRAC_INFO("No new DIMM temperature available for DIMM%04X (cur_temp[%d] flags[0x%02X])",
                              (l_port<<8)|l_dimm, l_fru->cur_temp, l_fru->flags);
                }

                //check if the temperature reading is still useable
                if(l_fru->temp_fru_type == DATA_FRU_DIMM)
                {
                   l_temp_timeout = g_amec->thermaldimm.temp_timeout;
                }

                else if(l_fru->temp_fru_type == DATA_FRU_MEMCTRL_DRAM)
                {
                   l_temp_timeout = g_amec->thermalmcdimm.temp_timeout;
                }

                else if(l_fru->temp_fru_type == DATA_FRU_PMIC)
                {
                   l_temp_timeout = g_amec->thermalpmic.temp_timeout;
                }

                else if(l_fru->temp_fru_type == DATA_FRU_MEMCTRL_EXT)
                {
                   l_temp_timeout = g_amec->thermalmcext.temp_timeout;
                }

                else // invalid type or not used, ignore
                   l_temp_timeout = 0xff;

                if(l_temp_timeout == 0xff ||
                   l_fru->sample_age < l_temp_timeout)
                {
                    continue;
                }

                //temperature has expired.  Notify control algorithms which DIMM DTS and type
                if(!(G_dimm_temp_expired_bitmap.bytes[l_port] & (DIMM_SENSOR0 >> l_dimm)))
                {
                    G_dimm_temp_expired_bitmap.bytes[l_port] |= (DIMM_SENSOR0 >> l_dimm);
                    TRAC_ERR("Timed out reading DIMM%04X temperature sensor type[0x%02X]",
                             (l_port<<8)|l_dimm,
                             l_fru->temp_fru_type);
                }

                //If we've already logged an error for this FRU go to the next one.
                if(G_dimm_timeout_logged_bitmap.bytes[l_port] & (DIMM_SENSOR0 >> l_dimm))
                {
                    continue;
                }

                // To prevent DIMMs from incorrectly being called out, don't log errors if there have
                // been timeouts with GPE1 tasks not finishing
                if(G_error_history[ERRH_GPE1_NOT_IDLE] > l_temp_timeout)
                {
                    TRAC_ERR("Timed out reading DIMM temperature due to GPE1 issues");
                    // give notification that GPE1 error should now be logged which will reset the OCC
                    G_log_gpe1_error = TRUE;
                    // no reason to check anymore since all DIMMs are collected from the same GPE
                    break;
                }

                TRAC_ERR("Timed out reading DIMM%04X temperature (cur_temp[%d] flags[0x%02X])",
                         (l_port<<8)|l_dimm, l_fru->cur_temp, l_fru->flags);

                //Mark DIMM as logged so we don't log it more than once
                amec_mem_mark_logged(l_port,
                                     l_dimm,
                                     &G_cent_timeout_logged_bitmap,
                                     &G_dimm_timeout_logged_bitmap.bytes[l_port]);

                // Create single elog with up to MAX_CALLOUTS
                if(l_callouts_count < ERRL_MAX_CALLOUTS)
                {
                    if(!l_err)
                    {
                        /* @
                         * @errortype
                         * @moduleid    AMEC_HEALTH_CHECK_DIMM_TIMEOUT
                         * @reasoncode  FRU_TEMP_TIMEOUT
                         * @userdata1   timeout value in seconds
                         * @userdata2   0
                         * @userdata4   ERC_AMEC_DIMM_TEMP_TIMEOUT
                         * @devdesc     Failed to read a memory DIMM temperature
                         *
                         */
                        l_err = createErrl(AMEC_HEALTH_CHECK_DIMM_TIMEOUT,    //modId
                                           FRU_TEMP_TIMEOUT,                  //reasoncode
                                           ERC_AMEC_DIMM_TEMP_TIMEOUT,        //Extended reason code
                                           ERRL_SEV_PREDICTIVE,               //Severity
                                           NULL,                              //Trace Buf
                                           DEFAULT_TRACE_SIZE,                //Trace Size
                                           l_temp_timeout,                    //userdata1
                                           0);                                //userdata2
                    }

                    //Get the HUID for the DIMM and add callout
                    l_huid = amec_mem_get_huid(l_port, l_dimm);
                    addCalloutToErrl(l_err,
                                     ERRL_CALLOUT_TYPE_HUID,
                                     l_huid,
                                     ERRL_CALLOUT_PRIORITY_MED);

                    l_callouts_count++;
                }
            } //iterate over all dimms
            if(G_log_gpe1_error)
            {
                // Going to be resetting so no reason to check anymore ports
                break;
            }
        } //iterate over all ports

        if(l_err)
        {
            commitErrl(&l_err);
        }

        //skip clearing if no dimms need it
        if( (!l_need_clr.dw[0]) && (!l_need_clr.dw[1]) )
        {
            break;
        }

        //iterate across all centaurs/ports clearing dimm sensor timers as needed
        for(l_port = 0; l_port < l_max_port; l_port++)
        {

            if(!l_need_clr.bytes[l_port])
            {
                continue;
            }

            //iterate over all dimms
            for(l_dimm = 0; l_dimm < l_max_dimm_per_port; l_dimm++)
            {
                //not this one, go to next one
                if(!(l_need_clr.bytes[l_port] & (DIMM_SENSOR0 >> l_dimm)))
                {
                    continue;
                }

                //we found one.
                l_fru = &g_amec->proc[0].memctl[l_port].centaur.dimm_temps[l_dimm];

                //clear timer
                l_fru->sample_age = 0;

                // In Simics: the RTL timer is increased and a DIMM reading will not always
                // complete on each call.  Skip the "recovery" trace in Simics.
                if((L_ran_once) && (!G_simics_environment))
                {
                    TRAC_INFO("DIMM temperature collection has resumed for DIMM%04X temp[%d]",
                              (l_port<<8)|l_dimm, l_fru->cur_temp);
                }

            }//iterate over all dimms
        }//iterate over all centaurs/ports
    }while(0);

    // For OCM the "DIMM" dts are used for different types.  Need to determine what type the
    // "DIMM" DTS readings are for so the control loop will handle timeout based on correct type
    if(MEM_TYPE_OCM == G_sysConfigData.mem_type)
    {
        if(G_dimm_temp_expired_bitmap.dw[0] || G_dimm_temp_expired_bitmap.dw[1])
        {
            // at least one sensor expired.  Set type for each expired sensor
            //iterate across all OCMBs
            for(l_port = 0; l_port < l_max_port; l_port++)
            {
               //iterate over all "dimm" DTS readings
               for(l_dimm = 0; l_dimm < l_max_dimm_per_port; l_dimm++)
               {
                   if(G_dimm_temp_expired_bitmap.bytes[l_port] & (DIMM_SENSOR0 >> l_dimm))
                   {
                      // found an expired sensor
                      l_ocm_dts_type_expired_bitmap |= g_amec->proc[0].memctl[l_port].centaur.dimm_temps[l_dimm].dts_type_mask;
                   }
               }//iterate over all dimms
            }//iterate over all OCMBs
        } // if temp expired

        // check if there is a change to any type expired
        if(G_ocm_dts_type_expired_bitmap != l_ocm_dts_type_expired_bitmap)
        {
            TRAC_INFO("DIMM DTS type expired bitmap changed from[0x%04X] to[0x%04X]",
                       G_ocm_dts_type_expired_bitmap, l_ocm_dts_type_expired_bitmap);
            G_ocm_dts_type_expired_bitmap = l_ocm_dts_type_expired_bitmap;
        }
    } // if mem type OCM

    L_ran_once = TRUE;

} // end amec_health_check_dimm_timeout()



/*
 * Function Specification
 *
 * Name: amec_health_check_cent_dimm_temp
 *
 * Description: Check if the centaur's dimm chips temperature exceeds the error
 *               temperature as defined in thermal control thresholds
 *              (ERROR field for Centaur FRU Type)
 *
 * End Function Specification
 */
void amec_health_check_cent_temp()
{
    /*------------------------------------------------------------------------*/
    /*  Local Variables                                                       */
    /*------------------------------------------------------------------------*/
    uint16_t                    l_ot_error, l_cur_temp, l_max_temp;
    sensor_t                    *l_sensor;
    uint32_t                    l_cent, l_max_mem_buf;
    uint32_t                    l_callouts_count = 0;
    uint16_t                    l_new_callouts;
    uint64_t                    l_huid;
    errlHndl_t                  l_err = NULL;

    /*------------------------------------------------------------------------*/
    /*  Code                                                                  */
    /*------------------------------------------------------------------------*/

    // Check to see if any centaurs have reached the error temperature that
    // haven't been called out already
    l_new_callouts = G_cent_overtemp_bitmap ^ G_cent_overtemp_logged_bitmap;
    if(!l_new_callouts)
    {
        return;
    }

    l_ot_error = g_amec->thermalcent.ot_error;
    l_sensor = getSensorByGsid(TEMPCENT);
    l_cur_temp = l_sensor->sample;
    l_max_temp = l_sensor->sample_max;
    TRAC_ERR("amec_health_check_cent_temp: Centaur reached error temp[%d]. current[%d], hist_max[%d], bitmap[0x%02X]",
             l_ot_error,
             l_cur_temp,
             l_max_temp,
             l_new_callouts);

    //find the centaur(s) that need to be called out
    if(G_sysConfigData.mem_type == MEM_TYPE_OCM)
    {
        l_max_mem_buf = MAX_NUM_OCMBS;
    }
    else // MEM_TYPE_CUMULUS
    {
        l_max_mem_buf = MAX_NUM_CENTAURS;
    }
    for(l_cent = 0; l_cent < l_max_mem_buf; l_cent++)
    {
        if(!(l_new_callouts & (CENTAUR0_PRESENT_MASK >> l_cent)))
        {
            continue;
        }

        l_huid = amec_mem_get_huid(l_cent, 0xff);

        amec_mem_mark_logged(l_cent,
                             0xff,
                             &G_cent_overtemp_logged_bitmap,
                             &G_dimm_overtemp_logged_bitmap.bytes[l_cent]);

        //If we don't have an error log for the callout, create one
        if(!l_err)
        {
            /* @
             * @errortype
             * @moduleid    AMEC_HEALTH_CHECK_CENT_TEMP
             * @reasoncode  CENT_ERROR_TEMP
             * @userdata1   Maximum centaur temperature
             * @userdata2   Centaur temperature threshold
             * @userdata4   OCC_NO_EXTENDED_RC
             * @devdesc     Centaur memory controller(s) exceeded maximum safe
             *              temperature.
             */
            l_err = createErrl(AMEC_HEALTH_CHECK_CENT_TEMP,    //modId
                               CENT_ERROR_TEMP,                //reasoncode
                               OCC_NO_EXTENDED_RC,             //Extended reason code
                               ERRL_SEV_PREDICTIVE,            //Severity
                               NULL,                           //Trace Buf
                               DEFAULT_TRACE_SIZE,             //Trace Size
                               l_max_temp,                     //userdata1
                               l_ot_error);                    //userdata2

            // Callout the "over temperature" procedure
            addCalloutToErrl(l_err,
                             ERRL_CALLOUT_TYPE_COMPONENT_ID,
                             ERRL_COMPONENT_ID_OVER_TEMPERATURE,
                             ERRL_CALLOUT_PRIORITY_HIGH);
            l_callouts_count = 1;
        }

        // Callout centaur
        addCalloutToErrl(l_err,
                         ERRL_CALLOUT_TYPE_HUID,
                         l_huid,
                         ERRL_CALLOUT_PRIORITY_MED);

        l_callouts_count++;

        //If we've reached the max # of callouts for an error log
        //commit the error log
        if(l_callouts_count == ERRL_MAX_CALLOUTS)
        {
            commitErrl(&l_err);
        }

    }//iterate over centaurs

    if(l_err)
    {
        commitErrl(&l_err);
    }
}

/*
 * Function Specification
 *
 * Name: amec_health_check_cent_timeout
 *
 * Description: Check for centaur timeout condition
 *              as defined in thermal control thresholds
 *              (MAX_READ_TIMEOUT field for Centaur FRU Type)
 *
 * End Function Specification
 */
void amec_health_check_cent_timeout()
{
    static uint16_t L_temp_update_bitmap_prev = 0;
    uint16_t l_need_inc, l_need_clr, l_temp_update_bitmap;
    uint16_t l_cent;
    fru_temp_t* l_fru;
    errlHndl_t  l_err = NULL;
    uint32_t    l_callouts_count = 0;
    uint64_t    l_huid;
    static bool L_ran_once = FALSE;

    do
    {
        //For every centaur sensor there are 3 cases to consider
        //
        //1) centaur is present and not updated (need to increment timer and check for timeout)
        //2) centaur is present and updated but wasn't updated on previous check (need to clear timer)
        //3) centaur is present and updated and was updated on previous check (do nothing)

        //Grab snapshot of G_cent_temp_update_bitmap and clear it
        l_temp_update_bitmap = G_cent_temp_updated_bitmap;
        G_cent_temp_updated_bitmap = 0;

        //check if we need to increment any timers
        l_need_inc = G_present_centaurs & ~l_temp_update_bitmap;

        //check if we need to clear any timers
        l_need_clr = l_temp_update_bitmap & ~L_temp_update_bitmap_prev;

        //only go further if we actually have work to do here.
        if(!l_need_inc && !l_need_clr)
        {
            //nothing to do
            break;
        }

        //save off the previous bitmap of updated sensors
        L_temp_update_bitmap_prev = l_temp_update_bitmap;

        //iterate across all centaurs incrementing timers as needed
        for(l_cent = 0; l_cent < MAX_NUM_CENTAURS; l_cent++)
        {
            //does this centaur timer need incrementing?
            if(!(l_need_inc & (CENTAUR0_PRESENT_MASK >> l_cent)))
            {
                //temperature was updated for this centaur. Clear the timeout bit for this centaur.
                if(G_cent_temp_expired_bitmap & (CENTAUR0_PRESENT_MASK >> l_cent))
                {
                    G_cent_temp_expired_bitmap &= ~(CENTAUR0_PRESENT_MASK >> l_cent);
                    TRAC_INFO("centaur %d temps have been updated", l_cent);
                }
                continue;
            }

            //This centaur requires an increment
            l_fru = &g_amec->proc[0].memctl[l_cent].centaur.centaur_hottest;

            //increment timer
            l_fru->sample_age++;

            //handle wrapping
            if(!l_fru->sample_age)
            {
                l_fru->sample_age = -1;
            }

            //info trace each transition to not having a new temperature
            if(l_fru->sample_age == 1)
            {
                TRAC_INFO("Failed to read centaur temperature on cent[%d] temp[%d] flags[0x%02X]",
                              l_cent, l_fru->cur_temp, l_fru->flags);
            }

            //check if the temperature reading is still useable
            if(g_amec->thermalcent.temp_timeout == 0xff ||
               l_fru->sample_age < g_amec->thermalcent.temp_timeout)
            {
                continue;
            }

            //temperature has expired.  Notify control algorithms which centaur.
            if(!(G_cent_temp_expired_bitmap & (CENTAUR0_PRESENT_MASK >> l_cent)))
            {
                G_cent_temp_expired_bitmap |= CENTAUR0_PRESENT_MASK >> l_cent;
                TRAC_ERR("Timed out reading centaur temperature sensor on cent %d",
                         l_cent);
            }

            //If we've already logged an error for this FRU go to the next one.
            if(G_cent_timeout_logged_bitmap & (CENTAUR0_PRESENT_MASK >> l_cent))
            {
                continue;
            }

            // To prevent Centaurs from incorrectly being called out, don't log errors if there have
            // been timeouts with GPE1 tasks not finishing
            if(G_error_history[ERRH_GPE1_NOT_IDLE] > g_amec->thermalcent.temp_timeout)
            {
                TRAC_ERR("Timed out reading centaur temperature due to GPE1 issues");
                // give notification that GPE1 error should now be logged which will reset the OCC
                G_log_gpe1_error = TRUE;
                // no reason to check anymore since all Centaurs are collected from the same GPE
                break;
            }

            TRAC_ERR("Timed out reading centaur temperature on cent[%d] temp[%d] flags[0x%02X]",
                     l_cent, l_fru->cur_temp, l_fru->flags);

            if(!l_err)
            {
                /* @
                 * @errortype
                 * @moduleid    AMEC_HEALTH_CHECK_CENT_TIMEOUT
                 * @reasoncode  FRU_TEMP_TIMEOUT
                 * @userdata1   timeout value in seconds
                 * @userdata2   0
                 * @userdata4   ERC_AMEC_CENT_TEMP_TIMEOUT
                 * @devdesc     Failed to read a centaur memory controller
                 *              temperature
                 *
                 */
                l_err = createErrl(AMEC_HEALTH_CHECK_CENT_TIMEOUT,    //modId
                                   FRU_TEMP_TIMEOUT,                  //reasoncode
                                   ERC_AMEC_CENT_TEMP_TIMEOUT,        //Extended reason code
                                   ERRL_SEV_PREDICTIVE,               //Severity
                                   NULL,                              //Trace Buf
                                   DEFAULT_TRACE_SIZE,                //Trace Size
                                   g_amec->thermalcent.temp_timeout,  //userdata1
                                   0);                                //userdata2

                l_callouts_count = 0;
            }

            //Get the HUID for the centaur
            l_huid = amec_mem_get_huid(l_cent, 0xff);

            // Callout centaur
            addCalloutToErrl(l_err,
                             ERRL_CALLOUT_TYPE_HUID,
                             l_huid,
                             ERRL_CALLOUT_PRIORITY_MED);

            l_callouts_count++;

            //If we've reached the max # of callouts for an error log
            //commit the error log
            if(l_callouts_count == ERRL_MAX_CALLOUTS)
            {
                commitErrl(&l_err);
            }

            //Mark centaur as logged so we don't log it more than once
            amec_mem_mark_logged(l_cent,
                                 0xff,
                                 &G_cent_timeout_logged_bitmap,
                                 &G_dimm_timeout_logged_bitmap.bytes[l_cent]);
        } //iterate over all centaurs

        if(l_err)
        {
            commitErrl(&l_err);
        }

        //skip clearing timers if no centaurs need it
        if(!l_need_clr)
        {
            break;
        }

        //iterate across all centaurs clearing timers as needed
        for(l_cent = 0; l_cent < MAX_NUM_CENTAURS; l_cent++)
        {
            //not this one, go to next one
            if(!(l_need_clr & (CENTAUR0_PRESENT_MASK >> l_cent)))
            {
                continue;
            }

            //we found one.
            l_fru = &g_amec->proc[0].memctl[l_cent].centaur.centaur_hottest;

            //clear timer
            l_fru->sample_age = 0;

            //info trace each time we recover
            if(L_ran_once)
            {
                TRAC_INFO("centaur temperature collection has resumed on cent[%d] temp[%d]",
                          l_cent, l_fru->cur_temp);
            }

        }//iterate over all centaurs
    }while(0);
    L_ran_once = TRUE;
}


// Function Specification
//
// Name:  amec_health_check_proc_temp
//
// Description: This function checks if the proc temperature has
// exceeded the error temperature as define in data format 0x13.
//
// End Function Specification
void amec_health_check_proc_temp()
{
    /*------------------------------------------------------------------------*/
    /*  Local Variables                                                       */
    /*------------------------------------------------------------------------*/
    uint16_t                    l_ot_error;
    static uint32_t             L_error_count = 0;
    static BOOLEAN              L_ot_error_logged = FALSE;
    sensor_t                    *l_sensor;
    errlHndl_t                  l_err = NULL;

    /*------------------------------------------------------------------------*/
    /*  Code                                                                  */
    /*------------------------------------------------------------------------*/
    do
    {
        // Get TEMPPROCTHRM sensor, which is hottest core temperature
        // in OCC processor
        l_sensor = getSensorByGsid(TEMPPROCTHRM);
        l_ot_error = g_amec->thermalproc.ot_error;

        // Check to see if we exceeded our error temperature
        if (l_sensor->sample > l_ot_error)
        {
            // Increment the error counter for this FRU
            L_error_count++;

            // Trace and log error the first time this occurs
            if (L_error_count == AMEC_HEALTH_ERROR_TIMER)
            {
                // Have we logged an OT error for this FRU already?
                if (L_ot_error_logged == TRUE)
                {
                    break;
                }

                L_ot_error_logged = TRUE;

                TRAC_ERR("amec_health_check_error_temp: processor has exceeded OT error! temp[%u] ot_error[%u]",
                         l_sensor->sample,
                         l_ot_error);

                // Log an OT error
                /* @
                 * @errortype
                 * @moduleid    AMEC_HEALTH_CHECK_PROC_TEMP
                 * @reasoncode  PROC_ERROR_TEMP
                 * @userdata1   0
                 * @userdata2   Fru peak temperature sensor
                 * @devdesc     Processor FRU has reached error temperature
                 *              threshold and is called out in this error log.
                 *
                 */
                l_err = createErrl(AMEC_HEALTH_CHECK_PROC_TEMP,
                                   PROC_ERROR_TEMP,
                                   ERC_AMEC_PROC_ERROR_OVER_TEMPERATURE,
                                   ERRL_SEV_PREDICTIVE,
                                   NULL,
                                   DEFAULT_TRACE_SIZE,
                                   0,
                                   l_sensor->sample_max);

                // Callout the Ambient procedure
                addCalloutToErrl(l_err,
                                 ERRL_CALLOUT_TYPE_COMPONENT_ID,
                                 ERRL_COMPONENT_ID_OVER_TEMPERATURE,
                                 ERRL_CALLOUT_PRIORITY_HIGH);

                // Callout to processor
                addCalloutToErrl(l_err,
                                 ERRL_CALLOUT_TYPE_HUID,
                                 G_sysConfigData.proc_huid,
                                 ERRL_CALLOUT_PRIORITY_MED);

                // Commit Error
                commitErrl(&l_err);
            }
        }
        else
        {
            // Trace that we have now dropped below the error threshold
            if (L_error_count >= AMEC_HEALTH_ERROR_TIMER)
            {
                TRAC_INFO("amec_health_check_proc_temp: We have dropped below error threshold for processors. error_count[%u]",
                          L_error_count);
            }

            // Reset the error counter for this FRU
            L_error_count = 0;
        }
    }while (0);

}

// Function Specification
//
// Name:  amec_health_check_proc_temp_timeout
//
// Description: This function checks if OCC has failed to read the processor
// temperature and if it has exceeded the maximum allowed number of retries.
//
// End Function Specification
void amec_health_check_proc_timeout()
{
    /*------------------------------------------------------------------------*/
    /*  Local Variables                                                       */
    /*------------------------------------------------------------------------*/
    errlHndl_t                  l_err = NULL;
    sensor_t                    *l_sensor = NULL;
    BOOLEAN                     l_core_fail_detected = FALSE;
    static uint32_t             L_read_fail_cnt = 0;
    uint8_t                     i = 0;
    uint8_t                     l_bad_core_index = 0;
    CoreData                    *l_core_data_ptr = NULL;

    /*------------------------------------------------------------------------*/
    /*  Code                                                                  */
    /*------------------------------------------------------------------------*/
    do
    {
        for(i=0; i<MAX_NUM_CORES; i++)
        {
            if(!CORE_PRESENT(i) || CORE_OFFLINE(i))
            {
                // If this core is not present, move on
                continue;
            }

            // Check if this core's temperature sensor has been updated
            l_sensor = AMECSENSOR_ARRAY_PTR(TEMPPROCTHRMC0,i);
            if (l_sensor->update_tag == G_core_temp_update_tag[i])
            {
                // If the update tag is not changing, then this core's
                // temperature sensor is not being updated.
                l_core_fail_detected = TRUE;
                l_bad_core_index = i;
            }

            // Take a snapshot of the update tag
            G_core_temp_update_tag[i] = l_sensor->update_tag;
        }

        // Have we found at least one core that has reading failures?
        if(!l_core_fail_detected)
        {
            // We were able to read all cores' temperature sensors so clear our
            // counter
            L_read_fail_cnt = 0;
        }
        else
        {
            // We've failed to read a core's temperature sensor so increment
            // our counter
            L_read_fail_cnt++;

            // Check if we have reached the maximum read time allowed
            if((L_read_fail_cnt == g_amec->thermalproc.temp_timeout) &&
               (g_amec->thermalproc.temp_timeout != 0xFF))
            {
                TRAC_ERR("Timed out reading processor temperature on core_index[%u]",
                         l_bad_core_index);

                // Get pointer to core data
                l_core_data_ptr = proc_get_bulk_core_data_ptr(l_bad_core_index);


                TRAC_ERR("Core Sensors[0x%04X%04X] Quad Sensor[0x%04X%04X]",
                         (uint16_t)(l_core_data_ptr->dts.core[0].result ),
                         (uint16_t)(l_core_data_ptr->dts.core[1].result ),
                         (uint16_t)(l_core_data_ptr->dts.cache[0].result),
                         (uint16_t)(l_core_data_ptr->dts.cache[1].result));

                /* @
                 * @errortype
                 * @moduleid    AMEC_HEALTH_CHECK_PROC_TIMEOUT
                 * @reasoncode  PROC_TEMP_TIMEOUT
                 * @userdata1   timeout value in seconds
                 * @userdata2   0
                 * @userdata4   OCC_NO_EXTENDED_RC
                 * @devdesc     Failed to read processor temperature.
                 *
                 */
                l_err = createErrl(AMEC_HEALTH_CHECK_PROC_TIMEOUT,    //modId
                                   PROC_TEMP_TIMEOUT,                 //reasoncode
                                   OCC_NO_EXTENDED_RC,                //Extended reason code
                                   ERRL_SEV_PREDICTIVE,               //Severity
                                   NULL,                              //Trace Buf
                                   DEFAULT_TRACE_SIZE,                //Trace Size
                                   g_amec->thermalproc.temp_timeout,  //userdata1
                                   0);                                //userdata2

                // Callout the processor
                addCalloutToErrl(l_err,
                                 ERRL_CALLOUT_TYPE_HUID,
                                 G_sysConfigData.proc_huid,
                                 ERRL_CALLOUT_PRIORITY_MED);

                // Commit error log and request reset
                REQUEST_RESET(l_err);
            }
        }
    }while(0);
}

// Function Specification
//
// Name:  amec_health_check_vrm_vdd_temp
//
// Description: This function checks if the VRM Vdd temperature has
// exceeded the error temperature sent in data format 0x13.
//
// End Function Specification
void amec_health_check_vrm_vdd_temp(const sensor_t *i_sensor)
{
    /*------------------------------------------------------------------------*/
    /*  Local Variables                                                       */
    /*------------------------------------------------------------------------*/
    uint16_t                    l_ot_error;
    static uint32_t             L_error_count = 0;
    static BOOLEAN              L_ot_error_logged = FALSE;
    errlHndl_t                  l_err = NULL;

    /*------------------------------------------------------------------------*/
    /*  Code                                                                  */
    /*------------------------------------------------------------------------*/
    do
    {
        l_ot_error = g_amec->thermalvdd.ot_error;

        // Check to see if we exceeded our error temperature
        if ((l_ot_error != 0) && (i_sensor->sample > l_ot_error))
        {
            // Increment the error counter for this FRU
            L_error_count++;

            // Trace and log error the first time this occurs
            if (L_error_count == AMEC_HEALTH_ERROR_TIMER)
            {
                // Have we logged an OT error for this FRU already?
                if (L_ot_error_logged == TRUE)
                {
                    break;
                }

                L_ot_error_logged = TRUE;

                TRAC_ERR("amec_health_check_vrm_vdd_temp: VRM vdd has exceeded OT error! temp[%u] ot_error[%u]",
                         i_sensor->sample,
                         l_ot_error);

                // Log an OT error
                /* @
                 * @errortype
                 * @moduleid    AMEC_HEALTH_CHECK_VRM_VDD_TEMP
                 * @reasoncode  VRM_VDD_ERROR_TEMP
                 * @userdata1   0
                 * @userdata2   Fru peak temperature sensor
                 * @devdesc     VRM Vdd has reached error temperature
                 *              threshold and is called out in this error log.
                 *
                 */
                l_err = createErrl(AMEC_HEALTH_CHECK_VRM_VDD_TEMP,
                                   VRM_VDD_ERROR_TEMP,
                                   ERC_AMEC_PROC_ERROR_OVER_TEMPERATURE,
                                   ERRL_SEV_PREDICTIVE,
                                   NULL,
                                   DEFAULT_TRACE_SIZE,
                                   0,
                                   i_sensor->sample_max);

                // Callout the Ambient procedure
                addCalloutToErrl(l_err,
                                 ERRL_CALLOUT_TYPE_COMPONENT_ID,
                                 ERRL_COMPONENT_ID_OVER_TEMPERATURE,
                                 ERRL_CALLOUT_PRIORITY_HIGH);

                // Callout VRM Vdd
                addCalloutToErrl(l_err,
                                 ERRL_CALLOUT_TYPE_HUID,
                                 G_sysConfigData.vrm_vdd_huid,
                                 ERRL_CALLOUT_PRIORITY_MED);

                // Commit Error
                commitErrl(&l_err);
            }
        }
        else
        {
            // Trace that we have now dropped below the error threshold
            if (L_error_count >= AMEC_HEALTH_ERROR_TIMER)
            {
                TRAC_INFO("amec_health_check_vrm_vdd_temp: VRM Vdd temp [%u] now below error temp [%u] after error_count [%u]",
                          i_sensor->sample, l_ot_error, L_error_count);
            }

            // Reset the error counter for this FRU
            L_error_count = 0;
        }
    }while (0);

}

// Function Specification
//
// Name:  amec_health_check_vrm_vdd_temp_timeout
//
// Description: This function checks if OCC has failed to read the VRM Vdd
// temperature and if it has exceeded the maximum allowed number of retries.
//
// End Function Specification
void amec_health_check_vrm_vdd_temp_timeout()
{
    /*------------------------------------------------------------------------*/
    /*  Local Variables                                                       */
    /*------------------------------------------------------------------------*/
    errlHndl_t                  l_err = NULL;
    uint32_t                    l_update_tag = 0;
    static uint32_t             L_read_fail_cnt = 0;
    static BOOLEAN              L_error_logged = FALSE;
    static uint32_t             L_vdd_temp_update_tag = 0;

    /*------------------------------------------------------------------------*/
    /*  Code                                                                  */
    /*------------------------------------------------------------------------*/

    // Check if VRM Vdd temperature sensor has been updated by checking the sensor update tag
    // If the update tag is not changing, then temperature sensor is not being updated.
    l_update_tag = AMECSENSOR_PTR(TEMPVDD)->update_tag;
    if (l_update_tag != L_vdd_temp_update_tag)
    {
        // We were able to read VRM Vdd temperature
        L_read_fail_cnt = 0;
        G_vrm_vdd_temp_expired = false;
        L_vdd_temp_update_tag = l_update_tag;
    }
    else
    {
        // Failed to read VRM Vdd temperature sensor
        L_read_fail_cnt++;

        // Check if we have reached the maximum read time allowed
        if((L_read_fail_cnt == g_amec->thermalvdd.temp_timeout) &&
           (g_amec->thermalvdd.temp_timeout != 0xFF))
        {
            //temperature has expired.  Notify control algorithms
            G_vrm_vdd_temp_expired = true;

            // Log error one time
            if (L_error_logged == FALSE)
            {
                L_error_logged = TRUE;

                TRAC_ERR("Timed out reading VRM Vdd temperature for timeout[%u]",
                          g_amec->thermalvdd.temp_timeout);

                /* @
                 * @errortype
                 * @moduleid    AMEC_HEALTH_CHECK_VRM_VDD_TIMEOUT
                 * @reasoncode  FRU_TEMP_TIMEOUT
                 * @userdata1   timeout value in seconds
                 * @userdata2   0
                 * @userdata4   ERC_AMEC_VRM_VDD_TEMP_TIMEOUT
                 * @devdesc     Failed to read VRM Vdd temperature.
                 *
                 */
                l_err = createErrl(AMEC_HEALTH_CHECK_VRM_VDD_TIMEOUT, //modId
                                   FRU_TEMP_TIMEOUT,                  //reasoncode
                                   ERC_AMEC_VRM_VDD_TEMP_TIMEOUT,     //Extended reason code
                                   ERRL_SEV_PREDICTIVE,               //Severity
                                   NULL,                              //Trace Buf
                                   DEFAULT_TRACE_SIZE,                //Trace Size
                                   g_amec->thermalvdd.temp_timeout,   //userdata1
                                   0);                                //userdata2

                // Callout the VRM
                addCalloutToErrl(l_err,
                                 ERRL_CALLOUT_TYPE_HUID,
                                 G_sysConfigData.vrm_vdd_huid,
                                 ERRL_CALLOUT_PRIORITY_MED);

                // Commit error log and request reset
                REQUEST_RESET(l_err);
            }
        } // if reached timeout
    } // else failed to read temp
}

/*----------------------------------------------------------------------------*/
/* End                                                                        */
/*----------------------------------------------------------------------------*/
