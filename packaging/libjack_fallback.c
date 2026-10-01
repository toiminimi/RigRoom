// Weak fallback stub for libjack.so.0
// Allows RigRoom to launch cleanly on Linux machines without PipeWire/JACK installed.
#include <stddef.h>

typedef void jack_client_t;
typedef void jack_port_t;
typedef unsigned int jack_nframes_t;
typedef int jack_options_t;
typedef int jack_status_t;

jack_client_t* jack_client_open(const char* client_name, jack_options_t options, jack_status_t* status, ...) {
    if (status) *status = 0x01; // JackServerFailed
    return NULL;
}

int jack_client_close(jack_client_t* client) { return 0; }
int jack_activate(jack_client_t* client) { return -1; }
int jack_deactivate(jack_client_t* client) { return 0; }
jack_nframes_t jack_get_sample_rate(jack_client_t* client) { return 48000; }
jack_nframes_t jack_get_buffer_size(jack_client_t* client) { return 256; }
const char* jack_get_client_name(jack_client_t* client) { return "RigRoom"; }

int jack_set_process_callback(jack_client_t* client, int (*callback)(jack_nframes_t, void*), void* arg) { return 0; }
int jack_set_buffer_size_callback(jack_client_t* client, int (*callback)(jack_nframes_t, void*), void* arg) { return 0; }
int jack_set_xrun_callback(jack_client_t* client, int (*callback)(void*), void* arg) { return 0; }
void jack_on_shutdown(jack_client_t* client, void (*callback)(void*), void* arg) {}

jack_port_t* jack_port_register(jack_client_t* client, const char* port_name, const char* port_type, unsigned long flags, unsigned long buffer_size) { return NULL; }
jack_port_t* jack_port_by_name(jack_client_t* client, const char* port_name) { return NULL; }
int jack_port_disconnect(jack_client_t* client, jack_port_t* port) { return 0; }
int jack_connect(jack_client_t* client, const char* source_port, const char* destination_port) { return -1; }
void* jack_port_get_buffer(jack_port_t* port, jack_nframes_t nframes) { return NULL; }
const char** jack_get_ports(jack_client_t* client, const char* port_name_pattern, const char* type_name_pattern, unsigned long flags) { return NULL; }
void jack_free(void* ptr) {}
int jack_set_buffer_size(jack_client_t* client, jack_nframes_t nframes) { return -1; }
float jack_cpu_load(jack_client_t* client) { return 0.0f; }

// MIDI input and the engine's MIDI ring buffer: without a server there are no
// events, and RigRoom checks the ring buffer for NULL before using it.
typedef void jack_ringbuffer_t;
unsigned int jack_midi_get_event_count(void* port_buffer) { return 0; }
int jack_midi_event_get(void* event, void* port_buffer, unsigned int event_index) { return -1; }
const char* jack_port_name(const jack_port_t* port) { return ""; }
jack_ringbuffer_t* jack_ringbuffer_create(size_t sz) { return NULL; }
void jack_ringbuffer_free(jack_ringbuffer_t* rb) {}
int jack_ringbuffer_mlock(jack_ringbuffer_t* rb) { return -1; }
size_t jack_ringbuffer_read(jack_ringbuffer_t* rb, char* dest, size_t cnt) { return 0; }
size_t jack_ringbuffer_read_space(const jack_ringbuffer_t* rb) { return 0; }
size_t jack_ringbuffer_write(jack_ringbuffer_t* rb, const char* src, size_t cnt) { return 0; }
size_t jack_ringbuffer_write_space(const jack_ringbuffer_t* rb) { return 0; }
