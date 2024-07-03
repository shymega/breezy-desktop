export const QUAT_X = 0;
export const QUAT_Y = 1;
export const QUAT_Z = 2;
export const QUAT_W = 3;

export function computeUnitTimeQuaternion(quatAtTime1, quatAtTime2, timeDifference) {
    // Compute the delta quaternion
    const deltaQuat = multiplyQuaternions(invertQuaternion(quatAtTime1), quatAtTime2);
    
    // Compute the unit time quaternion
    return quaternionPow(deltaQuat, 1.0 / timeDifference);
}

function quaternionPow(quat, exponent) {
    const theta = 2.0 * Math.acos(quat[QUAT_W]);
    if (theta === 0.0) return [0.0, 0.0, 0.0, 1.0];

    const axis = normalizeQuaternion([quat[QUAT_X], quat[QUAT_Y], quat[QUAT_Z], 0.0]);

    const newTheta = exponent * theta;
    const halfNewTheta = 0.5 * newTheta;
    const sineHalfTheta = Math.sin(halfNewTheta);
    const q = [
        axis[QUAT_X] * sineHalfTheta,
        axis[QUAT_Y] * sineHalfTheta,
        axis[QUAT_Z] * sineHalfTheta,
        Math.cos(halfNewTheta)
    ];

    return normalizeQuaternion(q);
}

function normalizeQuaternion(q) {
    const magnitude = Math.sqrt(magSquared(q));
    return [
        q[QUAT_X] / magnitude,
        q[QUAT_Y] / magnitude,
        q[QUAT_Z] / magnitude,
        q[QUAT_W] / magnitude
    ];
}

// Helper function to multiply two quaternions
function multiplyQuaternions(q1, q2) {
    return normalizeQuaternion([
        q1[QUAT_W] * q2[QUAT_X] + q1[QUAT_X] * q2[QUAT_W] + q1[QUAT_Y] * q2[QUAT_Z] - q1[QUAT_Z] * q2[QUAT_Y],
        q1[QUAT_W] * q2[QUAT_Y] - q1[QUAT_X] * q2[QUAT_Z] + q1[QUAT_Y] * q2[QUAT_W] + q1[QUAT_Z] * q2[QUAT_X],
        q1[QUAT_W] * q2[QUAT_Z] + q1[QUAT_X] * q2[QUAT_Y] - q1[QUAT_Y] * q2[QUAT_X] + q1[QUAT_Z] * q2[QUAT_W],
        q1[QUAT_W] * q2[QUAT_W] - q1[QUAT_X] * q2[QUAT_X] - q1[QUAT_Y] * q2[QUAT_Y] - q1[QUAT_Z] * q2[QUAT_Z]
    ]);
}

// Helper function to invert a quaternion
function invertQuaternion(q) {
    const magnitudeSquared = magSquared(q);
    return [
        -q[QUAT_X] / magnitudeSquared,
        -q[QUAT_Y] / magnitudeSquared,
        -q[QUAT_Z] / magnitudeSquared,
        q[QUAT_W] / magnitudeSquared
    ];
}

function magSquared(q) {
    return q[QUAT_X] * q[QUAT_X] + q[QUAT_Y] * q[QUAT_Y] + q[QUAT_Z] * q[QUAT_Z] + q[QUAT_W] * q[QUAT_W];
}