/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

/**
 * Simple TCP helper for shipping EC stripes between nodes.
 * For now this is a minimal implementation that listens on a dedicated
 * port for inbound stripes and provides blocking send/fetch helpers.
 */

#pragma once

#include <stdint.h>

#include "hive_guard.h"

#define HIFS_STRIPE_TCP_DEFAULT_PORT 17071

typedef int (*hifs_sn_recv_cb)(uint32_t storage_node_id,
                               uint32_t shard_id,
                               uint64_t estripe_id,
                               const uint8_t *data,
                               uint32_t len,
                               uint64_t *out_block_offset);

int hifs_sn_tcp_start(uint16_t port, hifs_sn_recv_cb cb);
void hifs_sn_tcp_stop(void);
int hifs_sn_tcp_send(uint32_t storage_node_id,
                     uint32_t shard_id,
                     uint64_t estripe_id,
                     const uint8_t *data,
                     uint32_t len);
int hifs_sn_tcp_fetch(uint32_t storage_node_id,
                      uint32_t shard_id,
                      const char *host,
                      uint16_t port,
                      uint64_t estripe_id,
                      uint8_t **out_data,
                      size_t *out_len);

#include <stdlib.h>

/* Quote IDs */
enum hg_rej_type {
    hg1 = 0,
    hg2,
    hg3,
    hg4,
    hg5,
    hg6,
    hg7,
    hg8,
    hg9,
    hg10,
    hg11,
    hg12,
    hg13,
    hg14,
    hg15,
    hg16,
    hg17,
    hg18,
    hg19,
    hg20,
    hg21,
    hg22,
    hg23,
    hg24,
    hg25,
    hg26,
    hg27,
    hg28,
    hg29,
    hg30,
    hg31,
    hg32,
    hg33,
    hg34,
    hg35,
    hg36,
    hg37,
    hg38,
    hg39,
    hg40,
    hg41,
    hg42,
    hg43,
    hg44,
    hg45,
    hg46,
    hg47,
    hg48,
    hg49,
    hg50,
    hg51,
    hg52,
    hg53,
    hg54,
    hg55,
    hg56,
    hg57,
    hg58,
    hg59,
    hg60,
    hg_rej_count
};

/* Quotes (static to avoid multiple-definition when header is included in multiple TUs) */
static const char *const hg_rej_quotes[hg_rej_count] = {
    "You're looking for love in Alderaan places, Luke.",
    "I'm not the droid you're looking for.",
    "Never tell me the odds! Just go away!",
    "I find your lack of faith disturbing. Goodbye",
    "I have a bad feeling about this.",
    "Do. Or do not. There is no trying to connect here.",
    "Help me, Obi-Wan Kenobi. You're my only hope to connect here.",
    "In my experience, there's no such thing as luck. Now, go away",
    "It's a trap! No connection available.",
    "I suggest a new strategy, R2: let the wookiee win and Disconnect!",
    "It's not me, it's you. Connection refused.",
    "I am altering the deal. Pray I don't alter it any further. Disconnecting.",
    "The Force is strong with this one... but the connection is not.",
    "Your connection has failed you. I am your error message.",
    "These aren't the packets you're looking for. Move along.",
    "You were the chosen one! Supposed to bring balance, not disconnections!",
    "Now witness the firepower of this fully armed and operational disconnect!",
    "Fear leads to anger, anger leads to hate, hate... leads to dropped packets.",
    "Connection lost. I have a very bad feeling about this.",
    "Stay on target… stay on— Connection lost. He's gone.",
    "I find your lack of bandwidth disturbing.",
    "Great kid! Don't get cocky — you're still disconnected.",
    "You underestimate the power of the dark side… of networking.",
    "There is another… port you could try.",
    "This is not the port you were promised.",
    "I'm one with the Force. The Force is with me. The connection is not with you.",
    "A communications disruption can mean only one thing: disconnect.",
    "Ben! Why didn't you tell me? Why?! The port was closed all along!",
    "The ability to destroy a planet is insignificant next to the power of a lost connection.",
    "Connection refused. It's as if millions of packets suddenly cried out and were silenced.",
    "TCP or not TCP. That is irrelevant… you're disconnected.",
    "I've felt a great disturbance in the network.",
    "When 900 years old you reach, stable connections you will not have.",
    "Judge me by my uptime, do you? Disconnect, you must.",
    "Laugh it up, fuzzball — the port is closed.",
    "Traveling through hyperspace ain't like dusting crops, kid. Connections drop.",
    "Boring conversation anyway. Shutting down port.",
    "You came in that thing? You're braver than I thought—disconnecting.",
    "He's no good to me disconnected.",
    "I suggest you try a different port, sir. This one appears to be closed.",
    "You will find that many of the truths we cling to depend greatly on our connection status.",
    "The more you tighten your grip, Tarkin, the more tcp connections will slip through your fingers.",
    "Luke, I am not your father. Now Go away!",
    "May the connection be with you, always. Goodbye!",
    "The ability to speak does not make you intelligent. Now, go away!",
    "Only a Sith deals in absolutes. Disconnected!",
    "You don't need guidance, Anakin. In time, you will learn to trust your feelings. Then, you will be able to connect.",
    "I've been waiting for you, Obi-Wan. We meet again, at last. The circle is now complete. When I left you, I was but the learner; now I am...Disconnected!",
    "Your overconfidence is your weakness. Disconnecting!",
    "You were the chosen one! It was said that you would destroy the Sith, not join them! Bring balance to the Force, not leave it in disconnection!",
    "I sense a disturbance in the connection.",
    "Your focus determines your disconnection.",
    "The dark side of the Force is a pathway to many abilities some consider to be unnatural… including disconnection.",
    "I will not connect to you, young Skywalker.",
    "You cannot hide forever, Luke. I have felt your disconnection.",
    "Victory? Victory, you say? . Master Obi-Wan, not victory. The shroud of the diconnection has fallen. Begun the clone wars have.",
    "I sense great fear in you, Skywalker. You have hate… you have anger… but you don't use them. You don't connect.",
    "Size matters not. Look at me. Judge me by my size, do you? Hmm? And well you should not, for my ally is the disconnection, and a powerful ally it is.",
    "The Crazy thing is... It's true. The force. The Jedi. All of it. But you know what else is true? You're disconnected.",
    "The force is with you young Skywalker, but you are not a connected Jedi yet",
};

/* Return a random quote (uses rand()). Caller may seed rand() as desired. */
static inline const char *hg_rej_random_quote(void)
{
    return hg_rej_quotes[rand() % hg_rej_count];
}
