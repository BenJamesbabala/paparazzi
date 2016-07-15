/*
 * Copyright (C) 2016 - IMAV 2016
 *
 * This file is part of Paparazzi.
 *
 * Paparazzi is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * Paparazzi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Paparazzi; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * @file modules/computer_vision/flight_plan_guided.c
 * @author IMAV 2016
 */

#include "modules/flight_plan_guided/flight_plan_guided.h"
#include "subsystems/ins.h"
#include "firmwares/rotorcraft/navigation.h"
#include "firmwares/rotorcraft/autopilot.h"
#include "firmwares/rotorcraft/guidance/guidance_h.h"
#include "firmwares/rotorcraft/guidance/guidance_v.h"
#include "modules/sonar/sonar_bebop.h"
#include "generated/flight_plan.h"
#include "autopilot.h"
#include <stdio.h>
#include <time.h>

#include "modules/computer_vision/marker/detector.h"
#include "modules/computer_vision/cv_georeference.h"


void flight_plan_guided_init(void) {} // Dummy


/* Kill throttle */
uint8_t KillEngines(void) {if (autopilot_mode == AP_MODE_GUIDED) { autopilot_set_motors_on(FALSE); } return false;}


/* Start throttle */
uint8_t StartEngines(void) {if (autopilot_mode == AP_MODE_GUIDED) { autopilot_set_motors_on(TRUE); } return false;}


/* Reset the altitude reference to the current GPS alt if GPS is used */
uint8_t ResetAlt(void) {if (autopilot_mode == AP_MODE_GUIDED) { ins_reset_altitude_ref(); } return false;}


bool TakeOff(float climb_rate) {
    if (autopilot_mode != AP_MODE_GUIDED) { return true; }

    guidance_v_set_guided_vz(-climb_rate);

    return false;
}

bool WaitUntilAltitude(float altitude) {
    if (autopilot_mode != AP_MODE_GUIDED) { return true; }

    if (stateGetPositionEnu_f()->z < altitude) { return true; }

    return false;
}

/* Take off */
uint8_t Hover(float altitude) {
    if (autopilot_mode != AP_MODE_GUIDED) { return true; }

    // Horizontal velocities are set to zero
    guidance_h_set_guided_body_vel(0, 0);

    // Vertical velocity increases until certain altitude is reached
    guidance_v_set_guided_z(-altitude);

    return false;
}

/* Move forward */
uint8_t MoveForward(float vx) {
    if (autopilot_mode == AP_MODE_GUIDED) {
        // Move forward
        guidance_h_set_guided_body_vel(vx, 0);
    }
    return false;
}


void marker_detection_periodic(void) {

    if (marker_detected) {

        struct camera_frame_t cam;
        cam.px = marker_pixel_x;
        cam.py = marker_pixel_y;
        cam.f = 400; // Focal length [px]
        cam.h = 240; // Frame height [px]
        cam.w = 240; // Frame width [px]

        georeference_project_target(&cam);

//        int delta_x = marker_pixel_x - 120; // +x = marker is right, bebop should go 'right'
//        int delta_y = marker_pixel_y - 120; // +y = marker is behind, bebop should go 'back'
//
        fprintf(stderr, "[DETECTOR] found! %i, %i\n", geo.target_rel.x, geo.target_rel.y);
//
//        float gain = 0.001;
//
//        // body velocity                 +x = +forward,   +y = +right
//        guidance_h_set_guided_body_vel(-delta_y * gain, delta_x * gain);
    }
}