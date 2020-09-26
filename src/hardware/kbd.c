#include "cpuapi.h"
#include "devices.h"
#include "pc.h"
#include "state.h"
#include <string.h>

// Intel 8042 Keyboard Controller
// http://zet.aluzina.org/images/d/d4/8042.pdf
// http://classiccomputers.info/down/IBM_PS2/documents/PS2_Hardware_Interface_Technical_Reference_May88.pdf (page 339)
// All commands are written first to 0x64, and then 0x60 (if needed)
// Some commands only make you write to 0x60

// Note that if keyboard/mouse are disabled, then their corresponding IRQs are disabled as well.

// HACK: We don't send mouse packets unless the last packet has been read

#define STATUS_OFULL 0x01
#define STATUS_SYSFLAG 0x04
#define STATUS_CMD 0x08
#define STATUS_KEYLOCK 0x10
#define STATUS_AUX 0x20
#define STATUS_TIME_OUT 0x40

#define CTRL_KEYDISABLED 0x10
#define CTRL_AUXDISABLED 0x20

#define COMMAND_TRANSLATE_MODE 0x40
#define COMMAND_AUX_DISABLED 0x20
#define COMMAND_KBD_DISABLED 0x10
#define COMMAND_ENABLE_AUX_INTR 0x02
#define COMMAND_ENABLE_KBD_INTR 0x01

#define MOUSE_LEFT 1
#define MOUSE_MIDDLE 4
#define MOUSE_RIGHT 2

#define KBD_LOG(x, ...) LOG("KBD", x, ##__VA_ARGS__)
#define KBD_FATAL(x, ...)          \
    do {                           \
        KBD_LOG(x, ##__VA_ARGS__); \
        ABORT();                   \
    } while (0)

struct kbd_queue {
    uint8_t data[256];
    uint8_t read_pos, write_pos;
};

enum {
    NO_COMMAND = 0
};

static void mouse_move(int clicked);

static void kbd_queue_add(struct kbd_queue* this, uint8_t data)
{
    this->data[this->write_pos] = data;
    this->write_pos = (this->write_pos + 1) & 0xFF;
}
static int kbd_queue_has(struct kbd_queue* this)
{
    return this->read_pos != this->write_pos;
}
static uint8_t kbd_queue_get(struct kbd_queue* this)
{
    uint8_t data = this->data[this->read_pos];
    this->read_pos = (this->read_pos + 1) & 0xFF;
    return data;
}
static void kbd_queue_clear(struct kbd_queue* this)
{
    this->read_pos = this->write_pos = 0;
}

// Listed in order of priority
#define KBD_QUEUE 0
#define AUX_QUEUE 1
#define NUMBER_OF_QUEUES 2
struct
{
    struct kbd_queue queues[2]; // TODO: Should there be a separate controller queue or can it be merged into the keyboard queue?

    // <<< BEGIN STRUCT "struct" >>>
    /// ignore: xrel, yrel

    // Data that is to be read from
    uint8_t ram[128]; // Internal controller RAM

    uint8_t data; // Data from port 60.
    int data_has_been_read; // Has the data from Port 60 been read yet?
    int current_interrupt_raised;

    int keyboard_disable_scanning;

    // Some important mouse status flags
    int mouse_scaling1to2; // If 0, then scaling is 1:1
    int mouse_stream_mode;
    int mouse_stream_inactive;
    int mouse_resolution;
    int mouse_sample_rate;

    int xrel, yrel;

    uint8_t status, command, keyboard_command, mouse_command;

    uint8_t mouse_button_state; // The current status of the mouse buttons held down

    // <<< END STRUCT "struct" >>>
} kbd;

static void kbd_queue_state(struct bjson_object* obj, struct kbd_queue* y, int z)
{
    char buffer[100];
    sprintf(buffer, "kbd.queues[%d].data", z);
    state_field(obj, 256, buffer, y->data);
    sprintf(buffer, "kbd.queues[%d].read_pos", z);
    state_field(obj, 2, buffer, &y->read_pos);
    sprintf(buffer, "kbd.queues[%d].write_pos", z);
    state_field(obj, 2, buffer, &y->write_pos);
}
static void kbd_state(void)
{
    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("kbd", 15 + 6);
    state_field(obj, 128, "kbd.ram", &kbd.ram);
    state_field(obj, 1, "kbd.data", &kbd.data);
    state_field(obj, 4, "kbd.data_has_been_read", &kbd.data_has_been_read);
    state_field(obj, 4, "kbd.current_interrupt_raised", &kbd.current_interrupt_raised);
    state_field(obj, 4, "kbd.keyboard_disable_scanning", &kbd.keyboard_disable_scanning);
    state_field(obj, 4, "kbd.mouse_scaling1to2", &kbd.mouse_scaling1to2);
    state_field(obj, 4, "kbd.mouse_stream_mode", &kbd.mouse_stream_mode);
    state_field(obj, 4, "kbd.mouse_stream_inactive", &kbd.mouse_stream_inactive);
    state_field(obj, 4, "kbd.mouse_resolution", &kbd.mouse_resolution);
    state_field(obj, 4, "kbd.mouse_sample_rate", &kbd.mouse_sample_rate);
    state_field(obj, 1, "kbd.status", &kbd.status);
    state_field(obj, 1, "kbd.command", &kbd.command);
    state_field(obj, 1, "kbd.keyboard_command", &kbd.keyboard_command);
    state_field(obj, 1, "kbd.mouse_command", &kbd.mouse_command);
    state_field(obj, 1, "kbd.mouse_button_state", &kbd.mouse_button_state);
    // <<< END AUTOGENERATE "state" >>>
    kbd_queue_state(obj, &kbd.queues[0], 0);
    kbd_queue_state(obj, &kbd.queues[1], 1);
    kbd_mouse_down(0, 0, 0); // Release all the mouse buttons
}

// returns 1 if there are items in the keyboard queue
// returns 2 if there are items in the mouse queue
// returns 0 if there are no items
static inline int kbd_items_in_queue(void)
{
    for (int i = 0; i < NUMBER_OF_QUEUES; i++) {
        struct kbd_queue* k = &kbd.queues[i];
        if (kbd_queue_has(k))
            return i + 1;
    }
    return 0;
}

// Raises either interrupt 1 or 12 based on the parameter "aux" if appropriate bits are set and sets status flags.
static void kbd_raise_irq(int aux)
{
    kbd.status |= STATUS_OFULL;
    if (aux)
        kbd.status |= STATUS_AUX;
    else
        kbd.status &= ~STATUS_AUX;

    int bit_to_test = 0x10 << aux; // 0x10 if aux is 0, 0x20 if aux is 1
    UNUSED(bit_to_test);
    if (
        //!(bit_to_test & kbd.ram[0]) && // Check if disabled bit is set to zero
        (kbd.ram[0] & (1 << aux)) // And IRQs are enabled for this device
        ) {
        int irqn = aux ? 12 : 1;
        pic_lower_irq(irqn);
        pic_raise_irq(irqn);
        kbd.current_interrupt_raised = irqn;
    }
}

// Refills the output buffer and raises an IRQ if necessary.
static void kbd_refill_output(void)
{
    if (!kbd.data_has_been_read) {
        int queue_irq;
        if ((queue_irq = kbd_items_in_queue())) {
            // Find out which queue they're in.
            if (queue_irq == 1) // Keyboard queue
                kbd_raise_irq(0); // Raise keyboard IRQ
            else // queue_irq == 2
                kbd_raise_irq(1);
        }
        return;
    }
    for (int i = 0; i < NUMBER_OF_QUEUES; i++) {
        struct kbd_queue* k = &kbd.queues[i];
        if (kbd_queue_has(k)) {
            // There is data available. Read it, and put it in the output buffer.
            kbd.data_has_been_read = 0;
            kbd.data = kbd_queue_get(k);

            // Raise IRQs as necessary and modify status bits.
            if (i == KBD_QUEUE) {
                kbd_raise_irq(0);
                break;
            } else {
                kbd_raise_irq(1);
                break;
            }
        }
    }
}

// Adds a byte to either the controller, keyboard, or mouse buffers.
static void kbd_add(int buffer, uint8_t data)
{
    kbd_queue_add(&kbd.queues[buffer], data);
    kbd_refill_output();
}

static uint32_t kbd_read(uint32_t port)
{
    if (port & 4) { // Status register (0x64)
        kbd.status &= ~STATUS_TIME_OUT;
        return kbd.status;
    } else { // Output buffer (0x60)
        // Clear both status bits
        kbd.status &= ~(STATUS_AUX | STATUS_OFULL);

        // Lower IRQ (this needs to be done before refilling the queue)
        if (kbd.current_interrupt_raised != -1) {
            pic_lower_irq(kbd.current_interrupt_raised);
            kbd.current_interrupt_raised = -1;
        }

        // Save the data to be returned since kbd_refill_output will have overwritten it anyways
        uint8_t data_to_return = kbd.data;

        // We have read the byte
        kbd.data_has_been_read = 1;

        // Refill the queue, if needed
        kbd_refill_output();

        return data_to_return;
    }
}

static void kbd_reset_port(int port)
{
    if (port == 0) {
        // First PS/2 port: Keyboard
        kbd.keyboard_disable_scanning = 0;
        kbd.ram[0] = 5;
    } else {
        // Second PS/2 port: Mouse
        kbd.mouse_scaling1to2 = 0;
        kbd.mouse_stream_mode = 1;
        kbd.mouse_stream_inactive = 1;
        kbd.mouse_resolution = 4;
        kbd.mouse_sample_rate = 100;
    }
}

static void kbd_reset(void)
{
    for (int i = 0; i < NUMBER_OF_QUEUES; i++)
        kbd_queue_clear(&kbd.queues[i]);
    kbd.data_has_been_read = 1;
    kbd.status = 0x18;
    kbd_reset_port(0);
    kbd_reset_port(1);
}

static void kbd_write(uint32_t port, uint32_t data)
{
    if (port & 4) { // Command Port (0x64)
        // This port can either run a command on its own or supply the first byte to a multi-byte command.
        kbd.status &= ~STATUS_CMD;
        //kbd.command = data;
        switch (data) {
        case 0x20 ... 0x3F: { // Read internal RAM, although only 0x20 is documented
            int address = kbd.command & 0x1F;
            if (address == 0)
                kbd_add(KBD_QUEUE, kbd.ram[0] | (kbd.status & STATUS_SYSFLAG));
            else
                kbd_add(KBD_QUEUE, kbd.ram[address]);
            break;
        }
        case 0x60 ... 0x7F: // Write to internal RAM - receives parameter
            kbd.status |= STATUS_CMD;
            kbd.command = data;
            break;
        case 0xF0 ... 0xFF: // Pulse output line
            if ((data & 1) == 0) {
                printf("System reset requested -- start the emulator again\n");
                exit(0);
            }
            break;
        case 0xA1: // Read keyboard BIOS version
            kbd_add(KBD_QUEUE, 0);
            //kbd.command = NO_COMMAND;
            break;
        case 0xA7 ... 0xA8: // Disable (A7)/Enable (A8) aux port
            kbd.ram[0] &= ~COMMAND_AUX_DISABLED;
            kbd.ram[0] |= (data & 1) ? COMMAND_AUX_DISABLED : 0;

            if (!(kbd.ram[0] & COMMAND_AUX_DISABLED)) {
                kbd_refill_output();
                if (!kbd.data_has_been_read)
                    kbd_raise_irq(1);
            }

            //kbd.command = NO_COMMAND;
            break;
        case 0xA9: // Test mouse port
            kbd_add(KBD_QUEUE, 0); // Note that even though it's testing the mouse port, it still comes in via the controller (AKA keyboard) queue
            //kbd.command = NO_COMMAND;
            break;
        case 0xAA: // Test PS/2 controller
            kbd.status |= STATUS_SYSFLAG;
            kbd.ram[0] |= STATUS_SYSFLAG; // Set it in the command byte as well.
            kbd_add(KBD_QUEUE, 0x55);
            //kbd.command = NO_COMMAND;
            break;
        case 0xAB: // Test keyboard port
            kbd_add(KBD_QUEUE, 0);
            //kbd.command = NO_COMMAND;
            break;
        case 0xAD ... 0xAE: // Disable (AD)/Enable (AE) Keyboard
            kbd.ram[0] &= ~COMMAND_KBD_DISABLED;
            kbd.ram[0] |= (data & 1) ? COMMAND_KBD_DISABLED : 0;

            // There may still be commands left over in the keyboard queue.
            // Check if keyboard is enabled AND queue has stuff in it.
            // TODO
            if (!(kbd.ram[0] & COMMAND_KBD_DISABLED)) {
                KBD_LOG("Attempting to refill queue: irqcur=%d dataread=%d data=%02x\n", kbd.current_interrupt_raised, kbd.data_has_been_read, kbd.data);
                kbd_refill_output();

                if (!kbd.data_has_been_read)
                    kbd_raise_irq(0);
            }
            //kbd.command = NO_COMMAND;
            break;
        case 0xC0: // Read input buffer
            kbd_add(KBD_QUEUE, 0x40);
            //kbd.command = NO_COMMAND;
            break;
        case 0xD1 ... 0xD4: // Outport utilities
            kbd.status |= STATUS_CMD;
            kbd.command = data;
            break;
        default:
            KBD_FATAL("TODO: Keyboard command %02x\n", data);
        }
    } else { // Data Port (0x60)
        // This port can be used for the following purposes
        //  - The first byte of a command being sent to the PS/2 Keyboard
        //  - The second byte of a command word, the first half of which was sent to 0x64
        //  - The second byte of a command word, the first half of which was sent to 0x60
        kbd.status &= ~STATUS_CMD;
        uint8_t command = kbd.command;
        kbd.command = NO_COMMAND;
        switch (command) {
        case NO_COMMAND:
            switch (data) {
            case 0xED: // Set LED state
                kbd_add(KBD_QUEUE, 0xFA);
                kbd.command = data;
                break;
            case 0xEE: // Echo
                kbd_add(KBD_QUEUE, 0xFA);
                break;
            case 0xF2: // Set LED state
                kbd_add(KBD_QUEUE, 0xFA);
                kbd_add(KBD_QUEUE, 0xAB);
                kbd_add(KBD_QUEUE, 0x41);
                break;
            case 0xF3: // Set typematic rate
                kbd.command = data;
                kbd_add(KBD_QUEUE, 0xFA); // ACK the command
                break;
            case 0xF4 ... 0xF5: // Enable (F4)/Disable (F5) scanning
                kbd.keyboard_disable_scanning = data & 1;
                kbd_add(KBD_QUEUE, 0xFA);
                break;
            case 0xF6: // Reset keyboard and enable scanning
                kbd_reset();
                kbd.keyboard_disable_scanning = 0;
                kbd_add(KBD_QUEUE, 0xFA);
                break;
            case 0xFF: // Reset and self-test
                kbd_reset_port(0);
                kbd_add(KBD_QUEUE, 0xFA);
                kbd_add(KBD_QUEUE, 0xAA);
                break;
            case 0xF0: // Get/set keyboard scancode set
                kbd.command = data;
                kbd_add(KBD_QUEUE, 0xFA);
                break;
            case 0x05: // Windows NT 4.0 uses this one, don't know what it does.
                kbd_add(KBD_QUEUE, 0xFE); // RESEND
                break;
            case 0xFA: // ATKBD_CMD_SETALL_MBR
            case 0xE8: // ATKBD "get id," according to Linux kernel sources
                kbd_add(AUX_QUEUE, 0xFE);
                break;
#if 0
            case 0:
                kbd_add(KBD_QUEUE, 0xFA);
                break;
#endif
            default:
                KBD_FATAL("TODO: Command %02x\n", data);
            }
            break;
        case 0x60 ... 0x7F: // Write to internal RAM
            if (command == 0x60) {
                //kbd.command = data;
                KBD_LOG("Command byte: %02x\n", kbd.command);
            }
            kbd.ram[command & 0x1F] = data;
            kbd_refill_output();
            break;
        case 0xD1: // Controller output gate
            cpu_set_a20(data >> 1 & 1);
            break;
        case 0xD2:
            kbd_add(KBD_QUEUE, data);
            break;
        case 0xD3: // Outport
            kbd_add(AUX_QUEUE, data);
            break;
        case 0xD4: // Write mouse status bits
            switch (kbd.mouse_command) {
            case 0xE8: // Resolution
                kbd.mouse_resolution = data;
                kbd_add(AUX_QUEUE, 0xFA);
                kbd.mouse_command = NO_COMMAND;
                break;
            case 0xF3: // Sample Rate
                kbd.mouse_sample_rate = data;
                kbd_add(AUX_QUEUE, 0xFA);
                kbd.mouse_command = NO_COMMAND;
                break;
            case NO_COMMAND:
                switch (data) {
                case 0xE6 ... 0xE7: // Set scaling 1:1 (0xE6) or 1:2 (0xE7)
                    kbd.mouse_scaling1to2 = data & 1;
                    kbd_add(AUX_QUEUE, 0xFA);
                    break;
                case 0xE8: // Set resolution
                    kbd.mouse_command = data;
                    kbd_add(AUX_QUEUE, 0xFA);
                    break;
                case 0xE9: // Send status
                    kbd_add(AUX_QUEUE, 0xFA);
                    kbd_add(AUX_QUEUE,
                        (kbd.mouse_stream_mode << 6) | (!kbd.mouse_stream_inactive << 5) | (!kbd.mouse_scaling1to2 << 4) | kbd.mouse_button_state);
                    kbd_add(AUX_QUEUE, kbd.mouse_resolution);
                    kbd_add(AUX_QUEUE, kbd.mouse_sample_rate);
                    break;
                case 0xEA: // Set stream mode
                    kbd.mouse_stream_mode = 1;
                    kbd_add(AUX_QUEUE, 0xFA);
                    break;
                case 0xF2: // Get mouse ID
                    kbd_add(AUX_QUEUE, 0xFA);
                    kbd_add(AUX_QUEUE, 0);
                    break;
                case 0xF3: // Set sample rate
                    kbd.mouse_command = data;
                    kbd_add(AUX_QUEUE, 0xFA);
                    break;
                case 0xF4 ... 0xF5: // Enable (F4)/disable (F5) packet
                    kbd.mouse_stream_inactive = data & 1;
                    kbd_add(AUX_QUEUE, 0xFA);
                    break;
                case 0xF6: // Set defaults
                    kbd_reset_port(1);
                    kbd_add(AUX_QUEUE, 0xFA);
                    break;
                case 0xFF: // Reset
                    kbd_reset_port(1);
                    kbd_add(AUX_QUEUE, 0xFA);
                    kbd_add(AUX_QUEUE, 0xAA);
                    kbd_add(AUX_QUEUE, 0x00);
                    break;
                case 0xBB: // ?? OS/2 Warp 4.5 uses this command
                    break;
                case 0xE1: // ?? TinyCore Linux uses this command
                case 0x0A:
                case 0x88:
                case 0:
                    kbd_add(AUX_QUEUE, 0xFE);
                    break;
                default:
                    KBD_FATAL("Unknown mouse command %02x\n", data);
                }
            }
            break;
        case 0xED: // Set keyboard LEDs
            kbd_add(KBD_QUEUE, 0xFA);
            kbd.keyboard_command = NO_COMMAND;
            break;
        case 0xF0: // Keyboard scancode set
            kbd_add(KBD_QUEUE, 0xFA);
            if (data == 0)
                kbd_add(KBD_QUEUE, 2);
            kbd.keyboard_command = NO_COMMAND;
            break;
        case 0xF3: // Set typematic rate
            kbd_add(KBD_QUEUE, 0xFA);
            kbd.keyboard_command = NO_COMMAND;
            break;
        default:
            KBD_FATAL("TODO: Keyboard command data byte command=%02x byte=%02x\n", command, data);
        }
    }
}

void kbd_init(void)
{
    io_register_reset(kbd_reset);
    io_register_read(0x60, 1, kbd_read, NULL, NULL);
    io_register_write(0x60, 1, kbd_write, NULL, NULL);
    io_register_read(0x64, 1, kbd_read, NULL, NULL);
    io_register_write(0x64, 1, kbd_write, NULL, NULL);
    state_register(kbd_state);
}

// Adds a key to the keyboard buffer.
void kbd_add_key(uint8_t data)
{
    if (!kbd.keyboard_disable_scanning) {
        kbd_add(KBD_QUEUE, data);
    }
}

#define CLAMP(t, x, too_big) \
    if (x > 255) {           \
        t = 255;             \
        res |= too_big;      \
    } else if (x < -255) {   \
        t = -255;            \
        res |= too_big;      \
    } else                   \
        t = x;

// This sends a mouse packet to the controller if something has changed.
static void mouse_move(int clicked)
{
    if (clicked || (kbd.xrel | kbd.yrel) != 0) {
        int dx, dy;
        kbd.yrel = -kbd.yrel;

        int res = 0x08;
        CLAMP(dx, kbd.xrel, 0x40);
        CLAMP(dy, kbd.yrel, 0x80);

        if (dy < 0) {
            res |= 0x20;
            dy &= 0xFF;
        }
        if (dx < 0) {
            res |= 0x10;
            dx &= 0xFF;
        }

        res |= kbd.mouse_button_state;

        kbd_add(AUX_QUEUE, res);
        kbd_add(AUX_QUEUE, dx);
        kbd_add(AUX_QUEUE, dy);

        kbd.xrel = 0;
        kbd.yrel = 0;
    }
}

void kbd_mouse_down(int left, int center, int right)
{
    uint8_t mbs = kbd.mouse_button_state;
    if (left != MOUSE_STATUS_NOCHANGE) {
        kbd.mouse_button_state &= ~MOUSE_LEFT;
        kbd.mouse_button_state |= -left & MOUSE_LEFT;
    }
    if (center != MOUSE_STATUS_NOCHANGE) {
        kbd.mouse_button_state &= ~MOUSE_MIDDLE;
        kbd.mouse_button_state |= -center & MOUSE_MIDDLE;
    }
    if (right != MOUSE_STATUS_NOCHANGE) {
        kbd.mouse_button_state &= ~MOUSE_RIGHT;
        kbd.mouse_button_state |= -right & MOUSE_RIGHT;
    }
    if (mbs ^ kbd.mouse_button_state) {
        mouse_move(1);
    }
}

void display_release_mouse(void);
void kbd_send_mouse_move(int xrel, int yrel)
{
    if (kbd.mouse_stream_mode && !kbd.mouse_stream_inactive) {
        kbd.xrel += xrel;
        kbd.yrel += yrel;
        // Don't send if we haven't read all the elements in the buffer yet.
        if (!kbd_queue_has(&kbd.queues[1]))
            mouse_move(0);
    }
}