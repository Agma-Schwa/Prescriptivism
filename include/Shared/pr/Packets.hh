#ifndef PRESCRIPTIVISM_PACKETS_HH
#define PRESCRIPTIVISM_PACKETS_HH

#define COMMON_PACKETS(X) \
    X(Disconnect)

#define SC_PACKETS(X) \
    X(HeartbeatRequest) \
    X(StartTurn) \
    X(EndTurn)

#define CS_PACKETS(X) \
    X(HeartbeatResponse) \
    X(Login)

#endif //PRESCRIPTIVISM_PACKETS_HH
