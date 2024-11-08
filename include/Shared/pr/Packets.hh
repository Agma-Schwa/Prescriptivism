#ifndef PRESCRIPTIVISM_PACKETS_HH
#define PRESCRIPTIVISM_PACKETS_HH

#define COMMON_PACKETS(X) \
    X(Disconnect)\
    X(WordChoice)

#define SC_PACKETS(X) \
    X(HeartbeatRequest) \
    X(StartTurn) \
    X(EndTurn) \
    X(Draw)

#define CS_PACKETS(X) \
    X(HeartbeatResponse) \
    X(Login)

#endif //PRESCRIPTIVISM_PACKETS_HH
