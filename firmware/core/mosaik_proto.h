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
#define MOSAIK_ID_CONFIG_BASE     0x280u  /* 0x281..0x283 (Lot 5 membership transaction) */
#define MOSAIK_ID_ACK_BASE        0x300u  /* 0x301..0x303 */

typedef enum {
    MOSAIK_MSG_UNKNOWN = 0,
    MOSAIK_MSG_SAFE,
    MOSAIK_MSG_VOTE_REQ,
    MOSAIK_MSG_VOTE_GRANT,
    MOSAIK_MSG_HEARTBEAT,
    MOSAIK_MSG_ACK,
    MOSAIK_MSG_CONFIG          /* Lot 5: membership / quorum reconfiguration */
} mosaik_msg_type_t;

/* Lot 5 CONFIG frame stages. A CONFIG frame reuses the 8-byte layout with
 * byte 2 = stage, byte 3 = membership mask (bit n-1 = node n), bytes 4..5 =
 * configuration epoch, byte 6 = argument (ACCEPT: proposer id). Its epoch
 * is a membership epoch, never a leadership term. This is the LOT 5 host
 * demonstrator encoding, not the final ADD CAN-FD ICD. */
typedef enum {
    MOSAIK_CFG_NONE     = 0,
    MOSAIK_CFG_PROPOSE  = 1,   /* proposer -> members: (new epoch, new mask) */
    MOSAIK_CFG_ACCEPT   = 2,   /* member -> proposer: acceptance bound to (epoch, mask) */
    MOSAIK_CFG_COMMIT   = 3,   /* proposer -> members: (epoch, mask) committed under the old quorum */
    MOSAIK_CFG_ANNOUNCE = 4    /* any node, periodic: its committed (epoch, mask) */
} mosaik_cfg_stage_t;

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
    uint16_t          term;    /* CONFIG: configuration epoch, not a leadership term */
    uint8_t           arg;     /* HEARTBEAT: seq. VOTE_GRANT: target id. SAFE: cause. CONFIG ACCEPT: proposer id. */
    uint8_t           cfg_stage; /* CONFIG only: mosaik_cfg_stage_t */
    uint8_t           cfg_mask;  /* CONFIG only: membership mask, bits 0..2 */
} mosaik_msg_t;

/* CRC-8/SAE-J1850 (poly 0x1D, init 0xFF, xorout 0xFF). */
uint8_t mosaik_crc8(const uint8_t *data, uint8_t len);

/* Serialise a message into a CAN frame. Always produces DLC 8. */
void mosaik_encode(mosaik_frame_t *frame, const mosaik_msg_t *msg);

/* Parse a CAN frame. Returns false on unknown identifier, wrong DLC,
 * bad protocol version, out-of-range source id, or CRC mismatch. For a
 * CONFIG frame the stage must be 1..4 and the mask non-empty within
 * bits 0..2; role and state are not carried and decode as FOLLOWER/INIT. */
bool mosaik_decode(const mosaik_frame_t *frame, mosaik_msg_t *msg);

#endif /* MOSAIK_PROTO_H */
