#include "gpio_ctrl.h"

static pthread_mutex_t g_gpio_mutex = PTHREAD_MUTEX_INITIALIZER;

static void sunxi_dump_write(const char *cmd) {
    int fd = open("/sys/class/sunxi_dump/write", O_WRONLY);
    if (fd >= 0) {
        write(fd, cmd, strlen(cmd));
        close(fd);
    }
}

int gpio_export(int gpio) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", gpio);
    if (access(path, F_OK) == 0) return 0; // Already exported

    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return -1;

    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d\n", gpio);
    write(fd, buf, len);
    close(fd);
    usleep(10000); // 10ms wait for udev/sysfs node creation
    return 0;
}

int gpio_set_direction(int gpio, const char *dir) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, dir, strlen(dir));
    close(fd);
    return 0;
}

int gpio_set_value(int gpio, int val) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, val ? "1\n" : "0\n", 2);
    close(fd);
    return 0;
}

int gpio_get_value(int gpio) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char val = '0';
    read(fd, &val, 1);
    close(fd);
    return val == '1' ? 1 : 0;
}

int gpio_init_all(void) {
    pthread_mutex_lock(&g_gpio_mutex);

    // Sunxi register config for PD22/PD23 IR-CUT driver strength
    sunxi_dump_write("0x42000358 0x00000003\n");
    sunxi_dump_write("0x420000ac 0x33111111\n");

    // Export output pins
    int outputs[] = { GPIO_IRCUT_ENA, GPIO_IRCUT_ENB, GPIO_IR_LED, GPIO_WHITE_LED, GPIO_SPK_EN, GPIO_BLE_EN };
    for (size_t i = 0; i < sizeof(outputs)/sizeof(outputs[0]); i++) {
        gpio_export(outputs[i]);
        gpio_set_direction(outputs[i], "out");
    }

    // Export input pins
    int inputs[] = { GPIO_RST_BTN, GPIO_KEY2 };
    for (size_t i = 0; i < sizeof(inputs)/sizeof(inputs[0]); i++) {
        gpio_export(inputs[i]);
        gpio_set_direction(inputs[i], "in");
    }

    // Default safe initial state
    gpio_set_value(GPIO_IRCUT_ENA, 0);
    gpio_set_value(GPIO_IRCUT_ENB, 0);
    gpio_set_value(GPIO_IR_LED, 0);
    gpio_set_value(GPIO_WHITE_LED, 0);
    gpio_set_value(GPIO_SPK_EN, 0);

    pthread_mutex_unlock(&g_gpio_mutex);

    // Apply default configured night mode
    set_night_mode(g_config.default_night_mode);
    return 0;
}

void gpio_cleanup_all(void) {
    pthread_mutex_lock(&g_gpio_mutex);
    gpio_set_value(GPIO_IRCUT_ENA, 0);
    gpio_set_value(GPIO_IRCUT_ENB, 0);
    gpio_set_value(GPIO_IR_LED, 0);
    gpio_set_value(GPIO_WHITE_LED, 0);
    gpio_set_value(GPIO_SPK_EN, 0);
    pthread_mutex_unlock(&g_gpio_mutex);
}

int set_ircut_filter(bool day_filter_on) {
    pthread_mutex_lock(&g_gpio_mutex);
    if (day_filter_on) {
        // Pulse ENA=1, ENB=0 to place IR-blocking filter in front of sensor
        gpio_set_value(GPIO_IRCUT_ENA, 1);
        gpio_set_value(GPIO_IRCUT_ENB, 0);
        usleep(50000); // 50ms pulse
        gpio_set_value(GPIO_IRCUT_ENA, 0);
        gpio_set_value(GPIO_IRCUT_ENB, 0);
    } else {
        // Pulse ENA=0, ENB=1 to retract IR-blocking filter (allow IR light)
        gpio_set_value(GPIO_IRCUT_ENA, 0);
        gpio_set_value(GPIO_IRCUT_ENB, 1);
        usleep(50000); // 50ms pulse
        gpio_set_value(GPIO_IRCUT_ENA, 0);
        gpio_set_value(GPIO_IRCUT_ENB, 0);
    }

    pthread_mutex_lock(&g_state_mutex);
    g_state.ircut_state = day_filter_on;
    pthread_mutex_unlock(&g_state_mutex);

    pthread_mutex_unlock(&g_gpio_mutex);
    LOG_INFO("IR Cut Filter: %s", day_filter_on ? "DAY (IR Blocked)" : "NIGHT (IR Passed)");
    return 0;
}

int set_ir_led(bool state) {
    pthread_mutex_lock(&g_gpio_mutex);
    gpio_set_value(GPIO_IR_LED, state ? 1 : 0);

    pthread_mutex_lock(&g_state_mutex);
    g_state.ir_led_state = state;
    pthread_mutex_unlock(&g_state_mutex);

    pthread_mutex_unlock(&g_gpio_mutex);
    LOG_INFO("Infrared LED: %s", state ? "ON" : "OFF");
    return 0;
}

int set_white_led(int brightness) {
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;

    pthread_mutex_lock(&g_gpio_mutex);
    // Simple digital on/off if PWM device node is not bound
    gpio_set_value(GPIO_WHITE_LED, brightness > 0 ? 1 : 0);

    pthread_mutex_lock(&g_state_mutex);
    g_state.white_led_brightness = brightness;
    pthread_mutex_unlock(&g_state_mutex);

    pthread_mutex_unlock(&g_gpio_mutex);
    LOG_INFO("White Spotlight LED: %d%%", brightness);
    return 0;
}

int set_night_mode(night_mode_t mode) {
    pthread_mutex_lock(&g_state_mutex);
    g_state.night_mode = mode;
    pthread_mutex_unlock(&g_state_mutex);

    switch (mode) {
        case NIGHT_MODE_DAY:
            set_ircut_filter(true);
            set_ir_led(false);
            set_white_led(0);
            LOG_INFO("Mode switched to: DAY");
            break;

        case NIGHT_MODE_IR:
            set_ircut_filter(false);
            set_ir_led(true);
            set_white_led(0);
            LOG_INFO("Mode switched to: NIGHT (Infrared)");
            break;

        case NIGHT_MODE_COLOR:
            set_ircut_filter(true);
            set_ir_led(false);
            set_white_led(100);
            LOG_INFO("Mode switched to: NIGHT (Color Spotlight)");
            break;

        case NIGHT_MODE_AUTO:
        default:
            LOG_INFO("Mode switched to: AUTO (Handled by light sensor loop)");
            break;
    }
    return 0;
}

int set_siren(bool state) {
    pthread_mutex_lock(&g_gpio_mutex);
    gpio_set_value(GPIO_SPK_EN, state ? 1 : 0);

    pthread_mutex_lock(&g_state_mutex);
    g_state.siren_state = state;
    pthread_mutex_unlock(&g_state_mutex);

    pthread_mutex_unlock(&g_gpio_mutex);
    LOG_INFO("Siren / Speaker Amp: %s", state ? "ENABLED" : "MUTED");
    return 0;
}
