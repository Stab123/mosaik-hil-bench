#include "mosaik_proto.h"

/* Payload layout, 8 bytes:
 *   [0] protocol version
 *   [1] source node id
 *   [2] role
 *   [3] state
 *   [4] term, low byte
 *   [5] term, high byte
 *   [6] arg (seq / vote target / safe cause)
 *   [7] CRC-8 over bytes 0..6
 */

uint8_t mosaik_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFFu;
    uint8_t i, b;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8; b++) {
            if (crc & 0x80u) {
                crc = (uint8_t)((crc << 1) ^ 0x1Du);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return (uint8_t)(crc ^ 0xFFu);
}

static uint32_t base_for_type(mosaik_msg_type_t type)
{
    switch (type) {
    case MOSAIK_MSG_SAFE:       return MOSAIK_ID_SAFE_BASE;
    case MOSAIK_MSG_VOTE_REQ:   return MOSAIK_ID_VOTE_REQ_BASE;
    case MOSAIK_MSG_VOTE_GRANT: return MOSAIK_ID_VOTE_GRANT_BASE;
    case MOSAIK_MSG_HEARTBEAT:  return MOSAIK_ID_HEARTBEAT_BASE;
    default:                    return 0u;
    }
}

void mosaik_encode(mosaik_frame_t *frame, const mosaik_msg_t *msg)
{
    frame->id  = base_for_type(msg->type) + msg->src;
    frame->dlc = MOSAIK_DLC;
    frame->data[0] = MOSAIK_PROTO_VERSION;
    frame->data[1] = msg->src;
    frame->data[2] = (uint8_t)msg->role;
    frame->data[3] = (uint8_t)msg->state;
    frame->data[4] = (uint8_t)(msg->term & 0xFFu);
    frame->data[5] = (uint8_t)((msg->term >> 8) & 0xFFu);
    frame->data[6] = msg->arg;
    frame->data[7] = mosaik_crc8(frame->data, 7);
}

bool mosaik_decode(const mosaik_frame_t *frame, mosaik_msg_t *msg)
{
    uint32_t offset;
    mosaik_msg_type_t type;

    if (frame->dlc != MOSAIK_DLC) {
        return false;
    }

    if (frame->id > MOSAIK_ID_HEARTBEAT_BASE &&
        frame->id <= MOSAIK_ID_HEARTBEAT_BASE + MOSAIK_MAX_NODES) {
        type = MOSAIK_MSG_HEARTBEAT;
        offset = MOSAIK_ID_HEARTBEAT_BASE;
    } else if (frame->id > MOSAIK_ID_VOTE_GRANT_BASE &&
               frame->id <= MOSAIK_ID_VOTE_GRANT_BASE + MOSAIK_MAX_NODES) {
        type = MOSAIK_MSG_VOTE_GRANT;
        offset = MOSAIK_ID_VOTE_GRANT_BASE;
    } else if (frame->id > MOSAIK_ID_VOTE_REQ_BASE &&
               frame->id <= MOSAIK_ID_VOTE_REQ_BASE + MOSAIK_MAX_NODES) {
        type = MOSAIK_MSG_VOTE_REQ;
        offset = MOSAIK_ID_VOTE_REQ_BASE;
    } else if (frame->id > MOSAIK_ID_SAFE_BASE &&
               frame->id <= MOSAIK_ID_SAFE_BASE + MOSAIK_MAX_NODES) {
        type = MOSAIK_MSG_SAFE;
        offset = MOSAIK_ID_SAFE_BASE;
    } else {
        return false;
    }

    if (frame->data[0] != MOSAIK_PROTO_VERSION) {
        return false;
    }
    if (frame->data[7] != mosaik_crc8(frame->data, 7)) {
        return false;
    }
    /* The identifier and the payload must agree on the source node. */
    if (frame->data[1] != (uint8_t)(frame->id - offset)) {
        return false;
    }
    if (frame->data[1] == 0u || frame->data[1] > MOSAIK_MAX_NODES) {
        return false;
    }
    if (frame->data[2] > (uint8_t)MOSAIK_ROLE_LEADER) {
        return false;
    }
    if (frame->data[3] > (uint8_t)MOSAIK_STATE_SAFE) {
        return false;
    }

    msg->type    = type;
    msg->version = frame->data[0];
    msg->src     = frame->data[1];
    msg->role    = (mosaik_role_t)frame->data[2];
    msg->state   = (mosaik_state_t)frame->data[3];
    msg->term    = (uint16_t)(frame->data[4] | ((uint16_t)frame->data[5] << 8));
    msg->arg     = frame->data[6];
    return true;
}
