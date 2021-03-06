#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

#define WIIMOTE_ACCELEROMETER (1 << 0)
#define WIIMOTE_BUTTONS (1 << 1)
#define WIIMOTE_IR (1 << 2)

#define UDPWM_B1 (1<<0)
#define UDPWM_B2 (1<<1)
#define UDPWM_BA (1<<2)
#define UDPWM_BB (1<<3)
#define UDPWM_BP (1<<4)
#define UDPWM_BM (1<<5)
#define UDPWM_BH (1<<6)
#define UDPWM_BU (1<<7)
#define UDPWM_BD (1<<8)
#define UDPWM_BL (1<<9)
#define UDPWM_BR (1<<10)
#define UDPWM_SK (1<<11)
#define UDPWM_NC (1<<0)
#define UDPWM_NZ (1<<1)

typedef struct Wiimote {
  int button_a;
  int button_b;
  int button_1;
  int button_2;
  int button_plus;
  int button_minus;
  int button_up;
  int button_down;
  int button_left;
  int button_right;
  int button_sk;
  int button_home;
  float accel_x;
  float accel_y;
  float accel_z;
  float ir_x;
  float ir_y;
} Wiimote;

typedef struct Server {
  unsigned short id;
  unsigned short port;
  unsigned char index;
  char name[256];
  unsigned char name_len;
  int sock;
  int broadcast_sock;
  unsigned char broadcast_buffer[512];
} Server;

void dump_state(const Wiimote *wm) {
  printf("\033[2J"
      "Up: %d\n"
      "Down: %d\n"
      "Left: %d\n"
      "Right: %d\n"
      "Plus: %d\n"
      "Minus: %d\n"
      "1: %d\n"
      "2: %d\n"
      "A: %d\n"
      "B: %d\n"
      "SK(\?\?\?\?): %d\n"
      "Home: %d\n"
      "Accel X: %f\n"
      "Accel Y: %f\n"
      "Accel Z: %f\n"
      "IR X: %f\n"
      "IR Y: %f\n",
      wm->button_up, wm->button_down, wm->button_left, wm->button_right,
      wm->button_plus, wm->button_minus,
      wm->button_1, wm->button_2,
      wm->button_a, wm->button_b, wm->button_sk, wm->button_home,
      wm->accel_x, wm->accel_y, wm->accel_z,
      wm->ir_x, wm->ir_y);
}

void build_broadcast_buffer(Server *srv) {
  unsigned char *buffer = srv->broadcast_buffer;
  unsigned short *id = (unsigned short *) (buffer+1);
  unsigned short *port = (unsigned short *) (buffer+4);

  buffer[0] = 0xdf;
  *id = htons(srv->id);
  buffer[3] = srv->index;
  *port = htons(srv->port);
  buffer[6] = srv->name_len;
  strncpy((char *) buffer+7, srv->name, 255);
}

void broadcast(const Server *srv) {
  struct sockaddr_in addr;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(4431);
  addr.sin_addr.s_addr =  INADDR_BROADCAST;

  sendto(srv->broadcast_sock, (const void *) srv->broadcast_buffer,
      srv->name_len+7, 0, (struct sockaddr *) &addr, sizeof(addr));
}

int main(int argc, char *argv[]) {
  struct sockaddr_in saddr;
  struct timeval timeout;
  unsigned char in_buffer[46];
  Server srv;
  Wiimote wm;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s PORT [INDEX] [NAME]\n", argv[0]);
    return 1;
  }
  srand(time(0));

  srv.id = (unsigned short) rand();
  srv.port = atoi(argv[1]);
  srv.index = (argc >= 3? atoi(argv[2]) : 0);
  strncpy(srv.name, (argc >= 4? argv[3] : "UDPWii Tester"), 255);
  srv.name[255] = 0;
  srv.name_len = strlen(srv.name);
  build_broadcast_buffer(&srv);

  memset(&wm, 0, sizeof(wm));

  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(srv.port);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);

  srv.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (srv.sock == -1) {
    fprintf(stderr, "Failed to open UDP socket\n");
    return 2;
  }
  if (bind(srv.sock, (struct sockaddr *) &saddr, sizeof(saddr)) == -1) {
    fprintf(stderr, "Failed to bind to port %d\n", srv.port);
    return 3;
  }

  srv.broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (srv.broadcast_sock == -1) {
    fprintf(stderr, "Failed to open broadcast socket\n");
    return 4;
  }
  {
    int broad = 1;
    if (setsockopt(srv.broadcast_sock, SOL_SOCKET, SO_BROADCAST,
          (const void *) &broad, sizeof(broad)) == -1) {
      fprintf(stderr, "Failed to enable broadcasting\n");
      return 5;
    }
  }

  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  for (;;) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(srv.sock, &read_fds);

    select(FD_SETSIZE, &read_fds, NULL, NULL, &timeout);

    if (FD_ISSET(srv.sock, &read_fds)) {
      recv(srv.sock, in_buffer, sizeof(in_buffer), 0);
      int o = 3;

      if (in_buffer[0] == 0xde) {
        if (in_buffer[2] & WIIMOTE_ACCELEROMETER) {
          int *p = (int *) (in_buffer+o);
          float ux = (float) ((int) ntohl(*(p++)));
          float uy = (float) ((int) ntohl(*(p++)));
          float uz = (float) ((int) ntohl(*(p++)));
          wm.accel_x = ux/1048576.f;
          wm.accel_y = uy/1048576.f;
          wm.accel_z = uz/1048576.f;
          o += 12;
        }
        if (in_buffer[2] & WIIMOTE_BUTTONS) {
          int *p = (int *) (in_buffer+o);
          int mask = (int) ntohl(*(p++));

          wm.button_a = (mask & UDPWM_BA? 1 : 0);
          wm.button_b = (mask & UDPWM_BB? 1 : 0);
          wm.button_1 = (mask & UDPWM_B1? 1 : 0);
          wm.button_2 = (mask & UDPWM_B2? 1 : 0);
          wm.button_plus = (mask & UDPWM_BP? 1 : 0);
          wm.button_minus = (mask & UDPWM_BM? 1 : 0);
          wm.button_up = (mask & UDPWM_BU? 1 : 0);
          wm.button_down = (mask & UDPWM_BD? 1 : 0);
          wm.button_left = (mask & UDPWM_BL? 1 : 0);
          wm.button_right = (mask & UDPWM_BR? 1 : 0);
          wm.button_sk = (mask & UDPWM_SK? 1 : 0);
          wm.button_home = (mask & UDPWM_BH? 1 : 0);

          o += 4;
        }
        if (in_buffer[2] & WIIMOTE_IR) {
          int *p = (int *) (in_buffer+o);
          float ux = (float) ((int) ntohl(*(p++)));
          float uy = (float) ((int) ntohl(*(p++)));
          wm.ir_x = ux/1048576.f;
          wm.ir_y = uy/1048576.f;
          o += 8;
        }
        dump_state(&wm);
      }
    }
    else {
      broadcast(&srv);
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;
    }
  }

  close(srv.sock);
  close(srv.broadcast_sock);

  return 0;
}
