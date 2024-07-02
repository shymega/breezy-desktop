#version 330 core

uniform sampler2D uDesktopTexture;
uniform sampler2D uCalibratingTexture;
uniform sampler2D uCustomBannerTexture;

uniform bool enabled;
uniform bool show_banner;
uniform float display_size;
uniform float display_north_offset;
uniform float lens_distance_ratio;
uniform bool sbs_enabled;
uniform bool sbs_content;
uniform bool sbs_mode_stretched;
uniform float half_fov_z_rads;
uniform float half_fov_y_rads;
uniform bool custom_banner_enabled;
uniform float trim_width_percent;
uniform float trim_height_percent;
uniform vec2 display_resolution;
uniform vec2 source_resolution;
uniform bool curved_display;

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

vec2 banner_position = vec2(0.5, 0.9);
float pi = 3.14159265359;

/**
 * For a curved display, our lenses are sitting inside a circle (defined by `radius`), at coords vectorStart and positioned 
 * as described by lookVector. Without moving vectorStart, and only changing the magnitude of the lookVector without changing
 * its direction, we need to find the scaling factor that will make the two vectors combined end up on the edge of the circle.
 *
 * The resulting magnitude of the combined vector -- created by putting our vectors tip-to-tail -- must be the radius
 * of the circle. Therefore: `radius = magnitude(lookVector*scale + vectorStart)`, where magnitude is
 * sqrt(vec.x^2 + vec.y^2).
 *
 * For simplicity: (x, y) = vectorStart, (a, b) = lookVector, r = radius, s = scale
 *
 * r^2 = (as+x)^2 + (bs+y)^2
 * 
 * Expanding and simplifying: (a^2 + b^2) * s^2 + 2(ax + by) * s + (x^2 + y^2 - r^2) = 0
 * 
 * This is a quadratic equation in the form of `ax^2 + bx + c = 0`, where we're solving for s (x) and:
 *  * `a = a^2 + b^2`
 *  * `b = 2(ax + by)`
 *  * `c = (x^2 + y^2 - r^2)`
 *
 * A negative return value is a "looking away" result
 **/
float getVectorScaleToCurve(float radius, vec2 vectorStart, vec2 lookVector) {
    float a = pow(lookVector.x, 2) + pow(lookVector.y, 2);
    float b = 2 * (lookVector.x * vectorStart.x + lookVector.y * vectorStart.y);
    float c = pow(vectorStart.x, 2) + pow(vectorStart.y, 2) - pow(radius, 2);

    float discriminant = pow(b, 2) - 4 * a * c;
    if (discriminant < 0.0) return -1.0;

    float sqrtDiscriminant = sqrt(discriminant);

    // return positive or largest, if both positive
    return max(
        (-b + sqrtDiscriminant) / (2 * a),
        (-b - sqrtDiscriminant) / (2 * a)
    );
}

void imu_adjust(in vec2 texcoord, out vec4 color) {
    vec2 tl = angle_tl;
    vec2 tr = angle_tr;
    vec2 bl = angle_bl;
    vec2 br = angle_br;
    vec3 lens_vector = rotated_lens_vector;
    vec2 x_limits = texcoord_x_limits;

    if(enabled && sbs_enabled) {
        bool right_display = texcoord.x > 0.5;

        if(right_display) {
            tl = angle_tl_r;
            tr = angle_tr_r;
            bl = angle_bl_r;
            br = angle_br_r;
            lens_vector = rotated_lens_vector_r;
            x_limits = texcoord_x_limits_r;
        }

        // translate the texcoord respresenting the current lens's half of the screen to a full-screen texcoord
        texcoord.x = (texcoord.x - (right_display ? 0.5 : 0.0)) * 2;
    }
    float texcoord_width = x_limits.y - x_limits.x;

    if(!enabled || show_banner) {
        bool banner_shown = false;
        if (show_banner) {
            vec2 banner_size = vec2(800.0 / display_resolution.x, 200.0 / display_resolution.y);

            // if the banner width is greater than the sreen width, scale it down
            banner_size /= max(banner_size.x, 1.1);

            vec2 banner_start = banner_position - banner_size / 2;

            // if the banner would extend too close or past the bottom edge of the screen, apply some padding
            banner_start.y = min(banner_start.y, 0.95 - banner_size.y);

            vec2 banner_texcoord = (texcoord - banner_start) / banner_size;
            if (banner_texcoord.x >= 0.0 && banner_texcoord.x <= 1.0 && banner_texcoord.y >= 0.0 && banner_texcoord.y <= 1.0) {
                banner_shown = true;
                if (custom_banner_enabled) {
                    color = texture2D(uCustomBannerTexture, banner_texcoord);
                } else {
                    color = texture2D(uCalibratingTexture, banner_texcoord);
                }
            }
        }
        
        if (!banner_shown) {
            // adjust texcoord back to the range that describes where the content is displayed
            texcoord.x = texcoord.x * texcoord_width + x_limits.x;

            color = texture2D(uDesktopTexture, texcoord);
        }
    } else {
        color = vec4(0, 0, 0, 1);

        // interpolate our texcoord between the four corner angular coordinates of the screen
        vec2 top = mix(tl, tr, texcoord.x);
        vec2 bottom = mix(bl, br, texcoord.x);
        vec2 angle = mix(top, bottom, texcoord.y);

        // divide all values by x to scale the magnitude so x is exactly 1, and multiply by the final display distance
        // so the vector is pointing at a coordinate on the screen
        bool looking_away = angle.x > pi/2 || angle.x < -pi/2 || angle.y > pi/2 || angle.y < -pi/2;
        if (!looking_away) {
            float display_distance = display_north_offset - lens_vector.x;

            // reconstruct the look vector from the angles interpolated from the vertex shader
            vec3 look_vector = vec3(1.0, tan(angle.x), tan(angle.y)) * display_distance + lens_vector;

            // deconstruct the rotated and scaled vector back to a texcoord (just inverse operations of the first conversion
            // above)
            texcoord.x = (fov_half_widths.x - look_vector.y) / fov_widths.x;
            texcoord.y = (fov_half_widths.y - look_vector.z) / fov_widths.y;

            // adjust texcoord back to the range that describes where the content is displayed
            texcoord.x = texcoord.x * texcoord_width + x_limits.x;

            // scale/zoom operations must always be done around the center
            vec2 texcoord_center = vec2(x_limits.x + texcoord_width/2.0f, 0.5f);
            texcoord -= texcoord_center;
            // scale the coordinates from aspect ratio of display to the aspect ratio of the source texture
            texcoord *= vec2(display_resolution.x / source_resolution.x, display_resolution.y / source_resolution.y);
            // apply the zoom
            texcoord /= display_size;
            texcoord += texcoord_center;

            if(texcoord.x >= x_limits.x + trim_width_percent && 
               texcoord.y >= trim_height_percent && 
               texcoord.x <= x_limits.y - trim_width_percent && 
               texcoord.y <= 1.0 - trim_height_percent && 
               (texcoord.x > 0.001 || texcoord.y > 0.002)) {
                color = texture2D(uDesktopTexture, texcoord);
            }
        }
    }
}