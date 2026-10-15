#include "motor_ctrl.h"

#define MOTOR_DEV_PATH "/dev/motor"
#define MOTOR_POS_FILE "/userdata/Config/MotorPos.cfg"

#define DEFAULT_MAX_X 3600
#define DEFAULT_MAX_Y 1200
#define DEFAULT_SPEED 5

static int g_motor_fd = -1;
static pthread_mutex_t g_motor_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_curr_x = 0;
static int g_curr_y = 0;

static void load_position(void) {
    FILE *f = fopen(MOTOR_POS_FILE, "r");
    if (f) {
        if (fscanf(f, "%d %d", &g_curr_x, &g_curr_y) == 2) {
            LOG_INFO("Loaded motor position: X=%d, Y=%d", g_curr_x, g_curr_y);
        }
        fclose(f);
    } else {
        g_curr_x = DEFAULT_MAX_X / 2;
        g_curr_y = DEFAULT_MAX_Y / 2;
    }
}

static void save_position(void) {
    FILE *f = fopen(MOTOR_POS_FILE, "w");
    if (f) {
        fprintf(f, "%d %d\n", g_curr_x, g_curr_y);
        fclose(f);
    }
}

int motor_init(void) {
    pthread_mutex_lock(&g_motor_mutex);
    g_motor_fd = open(MOTOR_DEV_PATH, O_RDWR);
    if (g_motor_fd < 0) {
        LOG_WARN("Could not open %s (%s). Running in simulation mode.", MOTOR_DEV_PATH, strerror(errno));
    } else {
        LOG_INFO("Successfully opened %s (fd=%d)", MOTOR_DEV_PATH, g_motor_fd);
        // Set initial speed
        int speed = g_config.default_motor_speed > 0 ? g_config.default_motor_speed : DEFAULT_SPEED;
        ioctl(g_motor_fd, MOTOR_IOCTL_SET_SPEED, speed);
    }
    load_position();
    pthread_mutex_unlock(&g_motor_mutex);
    return 0;
}

void motor_cleanup(void) {
    pthread_mutex_lock(&g_motor_mutex);
    if (g_motor_fd >= 0) {
        ioctl(g_motor_fd, MOTOR_IOCTL_STOP, 0);
        close(g_motor_fd);
        g_motor_fd = -1;
    }
    save_position();
    pthread_mutex_unlock(&g_motor_mutex);
}

int motor_move(motor_direction_t dir, int steps) {
    if (steps <= 0) {
        steps = g_config.motor_step_size > 0 ? g_config.motor_step_size : 100;
    }

    pthread_mutex_lock(&g_motor_mutex);
    pthread_mutex_lock(&g_state_mutex);
    g_state.is_moving = true;
    pthread_mutex_unlock(&g_state_mutex);

    int ret = 0;
    switch (dir) {
        case MOTOR_DIR_UP:
            g_curr_y += steps;
            if (g_curr_y > DEFAULT_MAX_Y) g_curr_y = DEFAULT_MAX_Y;
            if (g_motor_fd >= 0) {
                ret = ioctl(g_motor_fd, MOTOR_IOCTL_MOVE_VERT, steps);
            }
            LOG_INFO("Motor UP %d steps (Y=%d)", steps, g_curr_y);
            break;

        case MOTOR_DIR_DOWN:
            g_curr_y -= steps;
            if (g_curr_y < 0) g_curr_y = 0;
            if (g_motor_fd >= 0) {
                ret = ioctl(g_motor_fd, MOTOR_IOCTL_MOVE_VERT, -steps);
            }
            LOG_INFO("Motor DOWN %d steps (Y=%d)", steps, g_curr_y);
            break;

        case MOTOR_DIR_LEFT:
            g_curr_x -= steps;
            if (g_curr_x < 0) g_curr_x = 0;
            if (g_motor_fd >= 0) {
                ret = ioctl(g_motor_fd, MOTOR_IOCTL_MOVE_HORIZ, -steps);
            }
            LOG_INFO("Motor LEFT %d steps (X=%d)", steps, g_curr_x);
            break;

        case MOTOR_DIR_RIGHT:
            g_curr_x += steps;
            if (g_curr_x > DEFAULT_MAX_X) g_curr_x = DEFAULT_MAX_X;
            if (g_motor_fd >= 0) {
                ret = ioctl(g_motor_fd, MOTOR_IOCTL_MOVE_HORIZ, steps);
            }
            LOG_INFO("Motor RIGHT %d steps (X=%d)", steps, g_curr_x);
            break;

        case MOTOR_DIR_STOP:
            if (g_motor_fd >= 0) {
                ret = ioctl(g_motor_fd, MOTOR_IOCTL_STOP, 0);
            }
            LOG_INFO("Motor STOP requested");
            break;

        case MOTOR_DIR_HOME:
            LOG_INFO("Motor HOME requested");
            g_curr_x = DEFAULT_MAX_X / 2;
            g_curr_y = DEFAULT_MAX_Y / 2;
            if (g_motor_fd >= 0) {
                ioctl(g_motor_fd, MOTOR_IOCTL_STOP, 0);
                usleep(50000);
            }
            break;

        default:
            LOG_WARN("Unknown motor direction: %c", dir);
            ret = -1;
            break;
    }

    save_position();

    pthread_mutex_lock(&g_state_mutex);
    g_state.pos_x = g_curr_x;
    g_state.pos_y = g_curr_y;
    g_state.is_moving = false;
    pthread_mutex_unlock(&g_state_mutex);

    pthread_mutex_unlock(&g_motor_mutex);
    return ret;
}

int motor_move_continuous(motor_direction_t dir) {
    return motor_move(dir, 500);
}

int motor_stop(void) {
    return motor_move(MOTOR_DIR_STOP, 0);
}

int motor_reset_home(void) {
    return motor_move(MOTOR_DIR_HOME, 0);
}

int motor_set_speed(int speed) {
    if (speed < 1) speed = 1;
    if (speed > 10) speed = 10;
    pthread_mutex_lock(&g_motor_mutex);
    int ret = 0;
    if (g_motor_fd >= 0) {
        ret = ioctl(g_motor_fd, MOTOR_IOCTL_SET_SPEED, speed);
    }
    pthread_mutex_unlock(&g_motor_mutex);
    return ret;
}

int motor_get_position(int *out_x, int *out_y) {
    pthread_mutex_lock(&g_motor_mutex);
    if (out_x) *out_x = g_curr_x;
    if (out_y) *out_y = g_curr_y;
    pthread_mutex_unlock(&g_motor_mutex);
    return 0;
}

int motor_goto_position(int target_x, int target_y) {
    pthread_mutex_lock(&g_motor_mutex);
    int dx = target_x - g_curr_x;
    int dy = target_y - g_curr_y;
    pthread_mutex_unlock(&g_motor_mutex);

    if (dx > 0) motor_move(MOTOR_DIR_RIGHT, dx);
    else if (dx < 0) motor_move(MOTOR_DIR_LEFT, -dx);

    if (dy > 0) motor_move(MOTOR_DIR_UP, dy);
    else if (dy < 0) motor_move(MOTOR_DIR_DOWN, -dy);

    return 0;
}
