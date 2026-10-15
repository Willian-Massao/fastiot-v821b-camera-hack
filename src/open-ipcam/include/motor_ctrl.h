#ifndef MOTOR_CTRL_H
#define MOTOR_CTRL_H

#include "common.h"

// Motor ioctl definitions from disassembled motor.ko
#define MOTOR_IOC_MAGIC       'M'
#define MOTOR_IOCTL_MOVE_STEP   _IOW(MOTOR_IOC_MAGIC, 1, int)
#define MOTOR_IOCTL_GET_POS     _IOR(MOTOR_IOC_MAGIC, 2, int)
#define MOTOR_IOCTL_SET_SPEED   _IOW(MOTOR_IOC_MAGIC, 3, int)
#define MOTOR_IOCTL_MOVE_DUAL   _IOW(MOTOR_IOC_MAGIC, 5, struct motor_dual_move)
#define MOTOR_IOCTL_MOVE_HORIZ  _IOW(MOTOR_IOC_MAGIC, 15, int)
#define MOTOR_IOCTL_MOVE_VERT   _IOW(MOTOR_IOC_MAGIC, 16, int)
#define MOTOR_IOCTL_STOP        _IOW(MOTOR_IOC_MAGIC, 17, int)

struct motor_dual_move {
    int x_steps;
    int y_steps;
    int speed;
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
