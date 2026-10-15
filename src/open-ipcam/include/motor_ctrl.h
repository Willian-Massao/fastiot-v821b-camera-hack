#ifndef MOTOR_CTRL_H
#define MOTOR_CTRL_H

#include "common.h"

// Actual ioctl definitions from disassembled Cvitek/Allwinner motor.ko
#define MOTOR_IOCTL_STOP       0x80044d01
#define MOTOR_IOCTL_MOVE       0x80044d03
#define MOTOR_IOCTL_GET_STATUS 0x80044d04
#define MOTOR_IOCTL_SET_SPEED  0x80044d05
#define MOTOR_IOCTL_RESET_HOME 0x80044d06

struct motor_move_cmd {
    int x_steps;
    int y_steps;
};

struct motor_speed_cmd {
    int speed_x;
    int speed_y;
};

typedef enum {
    MOTOR_DIR_UP = 'u',
    MOTOR_DIR_DOWN = 'd',
    MOTOR_DIR_LEFT = 'l',
    MOTOR_DIR_RIGHT = 'r',
    MOTOR_DIR_STOP = 's',
    MOTOR_DIR_HOME = 'h'
} motor_direction_t;

int motor_init(void);
void motor_cleanup(void);
int motor_move(motor_direction_t dir, int steps);
int motor_move_continuous(motor_direction_t dir);
int motor_stop(void);
int motor_reset_home(void);
int motor_set_speed(int speed);
int motor_get_position(int *out_x, int *out_y);
int motor_goto_position(int x, int y);

#endif // MOTOR_CTRL_H
