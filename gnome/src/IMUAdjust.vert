#version 330 core

uniform bool enabled;
uniform bool show_banner;
uniform mat4 imu_quat_data;
uniform vec4 look_ahead_cfg;
uniform float look_ahead_ms;
uniform float display_size;
uniform float display_north_offset;
uniform float lens_distance_ratio;
uniform bool sbs_enabled;
uniform bool sbs_content;
uniform bool sbs_mode_stretched;
uniform float half_fov_z_rads;
uniform float half_fov_y_rads;
uniform vec2 source_resolution;
uniform vec2 display_resolution;

// texcoord values for the four corners of the screen, for the left eye if sbs
flat out vec2 texcoord_tl;
flat out vec2 texcoord_tr;
flat out vec2 texcoord_bl;
flat out vec2 texcoord_br;

// texcoord values for the four corners of the screen, for the right eye (not set if not sbs)
flat out vec2 texcoord_tl_r;
flat out vec2 texcoord_tr_r;
flat out vec2 texcoord_bl_r;
flat out vec2 texcoord_br_r;

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
vec3 applyLookAhead(vec3 position, vec3 velocity, float t
) {
    return position + velocity * t;
}

vec3 rateOfChange(
    in vec3 v1,
    in vec3 v2,
    in float delta_time
) {
    return (v1 - v2) / delta_time;
}

// Transforms from the origin texcoord (representing pixels on the headset's physical display) to the texcoord used
// to sample the desktop texture. It may go outside the normal bounds of a texcoord, in which case it's offscreen.
void texcoord_transform(in vec2 origin, in vec3 lens_vector, in float texcoord_x_min, in float texcoord_x_max, out vec2 texcoord) {
    texcoord = origin;
    if (enabled && !show_banner) {
        float fov_y_half_width = tan(half_fov_y_rads);
        float fov_y_width = fov_y_half_width * 2;
        float fov_z_half_width = tan(half_fov_z_rads);
        float fov_z_width = fov_z_half_width * 2;
        
        float vec_y = -texcoord.x * fov_y_width + fov_y_half_width;
        float vec_z = -texcoord.y * fov_z_width + fov_z_half_width;
        vec3 texcoord_vector = vec3(1.0, vec_y, vec_z);

        // then rotate the vector using each of the snapshots provided
        vec3 rotated_vector_t0 = applyQuaternionToVector(imu_quat_data[0], texcoord_vector);
        vec3 rotated_vector_t1 = applyQuaternionToVector(imu_quat_data[1], texcoord_vector);
        vec3 rotated_lens_vector = applyQuaternionToVector(imu_quat_data[0], lens_vector);

        // compute the velocity (units/ms) as change in the rotation snapshots
        float delta_time_t0 = imu_quat_data[3].x - imu_quat_data[3].y;
        vec3 velocity_t0 = rateOfChange(rotated_vector_t0, rotated_vector_t1, delta_time_t0);

        // allows for the bottom and top of the screen to have different look-ahead values
        float look_ahead_scanline_adjust = texcoord.y * look_ahead_cfg.z;

        // use the 4th value of the look-ahead config to cap the look-ahead value
        float look_ahead_ms_capped = min(min(look_ahead_ms, look_ahead_cfg.w), look_ahead_ms_cap) + look_ahead_scanline_adjust;

        // apply most recent velocity and acceleration to most recent position to get a predicted position
        vec3 res = applyLookAhead(rotated_vector_t0, velocity_t0, look_ahead_ms_capped) - rotated_lens_vector;

        if (res.x >= 0.0) {
            float display_distance = display_north_offset - rotated_lens_vector.x;

            // divide all values by x to scale the magnitude so x is exactly 1, and multiply by the final display distance
            // so the vector is pointing at a coordinate on the screen
            res *= display_distance / res.x;
            res += rotated_lens_vector;

            // deconstruct the rotated and scaled vector back to a texcoord (just inverse operations of the first conversion
            // above)
            texcoord.x = (fov_y_half_width - res.y) / fov_y_width;
            texcoord.y = (fov_z_half_width - res.z) / fov_z_width;

            // apply the texture offsets now
            float texcoord_width = texcoord_x_max - texcoord_x_min;
            texcoord.x = texcoord.x * texcoord_width + texcoord_x_min;

            // scale/zoom operations must always be done around the center
            vec2 texcoord_center = vec2(texcoord_x_min + texcoord_width/2.0f, 0.5f);
            texcoord -= texcoord_center;
            // scale the coordinates from aspect ratio of display to the aspect ratio of the source texture
            texcoord *= vec2(display_resolution.x / source_resolution.x, display_resolution.y / source_resolution.y);
            // apply the zoom
            texcoord /= display_size;
            texcoord += texcoord_center;
        } else {
            texcoord = vec2(-1.0, -1.0);
        }
    }
}