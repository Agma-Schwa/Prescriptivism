#ifndef PRESCRIPTIVISM_PACKETS_HH
#define PRESCRIPTIVISM_PACKETS_HH

#define COMMON_PACKETS(X) \
    X(Disconnect)         \
    X(WordChoice)

#define SC_PACKETS(X)   \
    X(HeartbeatRequest) \
    X(StartTurn)        \
    X(EndTurn)          \
    X(Draw)             \
    X(StartGame)        \
    X(AddSoundToStack)  \
    X(StackLockChanged)

#define CS_PACKETS(X)    \
    X(HeartbeatResponse) \
    X(Login)             \
    X(PlaySingleTarget)

#endif // PRESCRIPTIVISM_PACKETS_HH
