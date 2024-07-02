#version 330 core

uniform bool enabled;
uniform bool show_banner;
uniform mat4 imu_quat_data;
uniform vec4 look_ahead_cfg;
uniform float look_ahead_ms;
uniform float lens_distance_ratio;
uniform bool sbs_enabled;
uniform bool sbs_content;
uniform bool sbs_mode_stretched;
uniform float half_fov_z_rads;
uniform float half_fov_y_rads;
uniform vec2 source_resolution;
uniform vec2 display_resolution;

// none of these are actually varying, but gnome-shell's GLSL version doesn't support flat vertex output variables
// texcoord values for the four corners of the screen, for the left eye if sbs
varying vec2 angle_tl;
varying vec2 angle_tr;
varying vec2 angle_bl;
varying vec2 angle_br;

// texcoord values for the four corners of the right eye (not used if not sbs)
varying vec2 angle_tl_r;
varying vec2 angle_tr_r;
varying vec2 angle_bl_r;
varying vec2 angle_br_r;

varying vec3 rotated_lens_vector;
varying vec3 rotated_lens_vector_r; // right lens vector (not used if not sbs)
varying vec2 texcoord_x_limits;
varying vec2 texcoord_x_limits_r; // right eye texcoord limits (not used if not sbs)

varying vec2 fov_half_widths;
varying vec2 fov_widths; 

float look_ahead_ms_cap = 45.0;

vec4 quatMul(vec4 q1, vec4 q2) {
    vec3 u = vec3(q1.x, q1.y, q1.z);
    float s = q1.w;
    vec3 v = vec3(q2.x, q2.y, q2.z);
    float t = q2.w;
    return vec4(s * v + t * u + cross(u, v), s * t - dot(u, v));
}

vec4 quatConj(vec4 q) {
    return vec4(-q.x, -q.y, -q.z, q.w);
}

vec3 applyQuaternionToVector(vec4 q, vec3 v) {
    vec4 p = quatMul(quatMul(q, vec4(v, 0)), quatConj(q));
    return p.xyz;
}

const int day_in_seconds = 24 * 60 * 60;

// attempt to figure out where the current position should be based on previous position and velocity.
// velocity and time values should use the same time units (secs, ms, etc...)
vec3 applyLookAhead(vec3 position, vec3 velocity, float t) {
    return position + velocity * t;
}

vec3 rateOfChange(in vec3 v1, in vec3 v2, in float delta_time) {
    return (v1 - v2) / delta_time;
}

// Transforms from the origin texcoord (representing pixels on the headset's physical display) to the look angle
// that the headset should be displaying at that pixel after rotation. Apply this to each corner of the display
// and then interpolate between the corners in the fragment shader.
vec2 texcoord_angle_transform(vec2 texcoord, vec3 rotated_lens_vector, vec2 half_widths, vec2 widths) {
    float vec_y = -texcoord.x * widths.x + half_widths.x;
    float vec_z = -texcoord.y * widths.y + half_widths.y;
    vec3 texcoord_vector = vec3(1.0, vec_y, vec_z);

    if (enabled && !show_banner) {       
        // then rotate the vector using each of the snapshots provided
        vec3 rotated_vector_t0 = applyQuaternionToVector(imu_quat_data[0], texcoord_vector);
        vec3 rotated_vector_t1 = applyQuaternionToVector(imu_quat_data[1], texcoord_vector);

        // compute the velocity (units/ms) as change in the rotation snapshots
        float delta_time_t0 = imu_quat_data[3].x - imu_quat_data[3].y;
        vec3 velocity_t0 = rateOfChange(rotated_vector_t0, rotated_vector_t1, delta_time_t0);

        // allows for the bottom and top of the screen to have different look-ahead values
        float look_ahead_scanline_adjust = texcoord.y * look_ahead_cfg.z;

        // use the 4th value of the look-ahead config to cap the look-ahead value
        float look_ahead_ms_capped = min(min(look_ahead_ms, look_ahead_cfg.w), look_ahead_ms_cap) + look_ahead_scanline_adjust;

        // apply most recent velocity and acceleration to most recent position to get a predicted position
        texcoord_vector = applyLookAhead(rotated_vector_t0, velocity_t0, look_ahead_ms_capped) - rotated_lens_vector;
    }

    // res follows a tan curve, we need to convert the vector into radians to be linearly interpolated,
    // magnitude can't scale linearly, so we'll have to recompute distance in the fragment shader
    return vec2(atan(texcoord_vector.y, texcoord_vector.x), atan(texcoord_vector.z, texcoord_vector.x));
}