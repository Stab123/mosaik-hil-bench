/* MOSAIK HIL bench - wire protocol
 * Reference: PROTOCOL.md, issue 0.1
 * Portable C99. No OS, no heap, no floating point.
 */
#ifndef MOSAIK_PROTO_H
#define MOSAIK_PROTO_H

#include <stdint.h>
#include <stdbool.h>

#define MOSAIK_PROTO_VERSION 0x01u
#define MOSAIK_MAX_NODES     3u
#define MOSAIK_DLC           8u

/* CAN 11-bit identifiers. Lower numeric value wins bus arbitration,
 * so safety-critical traffic is allocated the lowest identifiers. */
#define MOSAIK_ID_SAFE_BASE       0x080u  /* 0x081..0x083 */
#define MOSAIK_ID_VOTE_REQ_BASE   0x100u  /* 0x101..0x103 */
#define MOSAIK_ID_VOTE_GRANT_BASE 0x180u  /* 0x181..0x183 */
#define MOSAIK_ID_HEARTBEAT_BASE  0x200u  /* 0x201..0x203 */

typedef enum {
    MOSAIK_MSG_UNKNOWN = 0,
    MOSAIK_MSG_SAFE,
    MOSAIK_MSG_VOTE_REQ,
    MOSAIK_MSG_VOTE_GRANT,
    MOSAIK_MSG_HEARTBEAT
} mosaik_msg_type_t;

typedef enum {
    MOSAIK_ROLE_FOLLOWER  = 0,
    MOSAIK_ROLE_CANDIDATE = 1,
    MOSAIK_ROLE_LEADER    = 2
} mosaik_role_t;

typedef enum {
    MOSAIK_STATE_INIT     = 0,
    MOSAIK_STATE_NOMINAL  = 1,
    MOSAIK_STATE_DEGRADED = 2,
    MOSAIK_STATE_SAFE     = 3
} mosaik_state_t;

/* SAFE cause codes, reported in the frame payload. */
typedef enum {
    MOSAIK_SAFE_NONE        = 0,
    MOSAIK_SAFE_SPLIT_BRAIN = 1, /* two leaders observed in the same term */
    MOSAIK_SAFE_NO_QUORUM   = 2, /* repeated elections without quorum */
    MOSAIK_SAFE_PROTO_ERROR = 3  /* persistent decode failures on the bus */
} mosaik_safe_cause_t;

typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[MOSAIK_DLC];
} mosaik_frame_t;

typedef struct {
    mosaik_msg_type_t type;
    uint8_t           src;     /* 1..MOSAIK_MAX_NODES */
    uint8_t           version;
    mosaik_role_t     role;
    mosaik_state_t    state;
    uint16_t          term;
    uint8_t           arg;     /* HEARTBEAT: seq. VOTE_GRANT: target id. SAFE: cause. */
} mosaik_msg_t;

/* CRC-8/SAE-J1850 (poly 0x1D, init 0xFF, xorout 0xFF). */
uint8_t mosaik_crc8(const uint8_t *data, uint8_t len);

/* Serialise a message into a CAN frame. Always produces DLC 8. */
void mosaik_encode(mosaik_frame_t *frame, const mosaik_msg_t *msg);

/* Parse a CAN frame. Returns false on unknown identifier, wrong DLC,
 * bad protocol version, out-of-range source id, or CRC mismatch. */
bool mosaik_decode(const mosaik_frame_t *frame, mosaik_msg_t *msg);

#endif /* MOSAIK_PROTO_H */
