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
uniform bool custom_banner_enabled;
uniform float trim_width_percent;
uniform float trim_height_percent;
uniform vec2 display_resolution;
uniform bool curved_display;

// texcoord values for the four corners of the screen, for the left eye if sbs
flat in vec2 texcoord_tl;
flat in vec2 texcoord_tr;
flat in vec2 texcoord_bl;
flat in vec2 texcoord_br;

// texcoord values for the four corners of the screen, for the right eye (not set if not sbs)
flat in vec2 texcoord_tl_r;
flat in vec2 texcoord_tr_r;
flat in vec2 texcoord_bl_r;
flat in vec2 texcoord_br_r;

vec2 banner_position = vec2(0.5, 0.9);

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
    float texcoord_x_min = 0.0;
    float texcoord_x_max = 1.0;
    vec2 vertex_tl = texcoord_tl;
    vec2 vertex_tr = texcoord_tr;
    vec2 vertex_bl = texcoord_bl;
    vec2 vertex_br = texcoord_br;

    if(enabled && sbs_enabled) {
        bool right_display = texcoord.x > 0.5;

        if(right_display) {
            vertex_tl = texcoord_tl_r;
            vertex_tr = texcoord_tr_r;
            vertex_bl = texcoord_bl_r;
            vertex_br = texcoord_br_r;
        }

        if(sbs_content) {
            // source video is SBS, left-half of the screen goes to the left lens, right-half to the right lens
            if(right_display)
                texcoord_x_min = 0.5;
            else
                texcoord_x_max = 0.5;
        }
        if(!sbs_mode_stretched) {
            // if the content isn't stretched, assume it's centered in the middle 50% of the screen
            texcoord_x_min = max(0.25, texcoord_x_min);
            texcoord_x_max = min(0.75, texcoord_x_max);
        }

        // translate the texcoord respresenting the current lens's half of the screen to a full-screen texcoord
        texcoord.x = (texcoord.x - (right_display ? 0.5 : 0.0)) * 2;
    }

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
            float texcoord_width = texcoord_x_max - texcoord_x_min;
            texcoord.x = texcoord.x * texcoord_width + texcoord_x_min;

            color = texture2D(uDesktopTexture, texcoord);
        }
    } else {
        // interpolate our sample point between the four corners of the screen
        vec2 vertex_top = mix(vertex_tl, vertex_tr, texcoord.x);
        vec2 vertex_bottom = mix(vertex_bl, vertex_br, texcoord.x);
        vec2 final_texcoord = mix(vertex_top, vertex_bottom, texcoord.y);

        if(final_texcoord.x < texcoord_x_min + trim_width_percent || 
           final_texcoord.y < trim_height_percent || 
           final_texcoord.x > texcoord_x_max - trim_width_percent || 
           final_texcoord.y > 1.0 - trim_height_percent || 
           final_texcoord.x <= 0.001 && final_texcoord.y <= 0.002) {
            color = vec4(0, 0, 0, 1);
        } else {
            color = texture2D(uDesktopTexture, final_texcoord);
        }
    }
}