#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
/* Include the real service to exercise one TX step without starting a task. */
#include <services/mist/mist_service.c>

static bool client_busy;
static int send_error;
static uint8_t sent[32];
static unsigned sent_count;
int mist_board_client_init(void) { return 0; }
int mist_board_client_trigger_otp_reset(void) { return 0; }
void mist_board_client_process_rx(TickType_t timeout) { (void)timeout; }
void mist_board_client_process_timeouts(void) {}
void mist_board_client_get_status(mist_board_status_t *s) { memset(s, 0, sizeof(*s)); }
void app_context_set_mist_status(const mist_board_status_t *s) { (void)s; }
int app_event_submit(const app_event_t *evt) { (void)evt; return 0; }
void platform_log(const char *level, const char *format, ...) { (void)level; (void)format; }
int mist_board_client_submit(const struct mist_client_request *req)
{
    if (client_busy) return -EBUSY;
    if (send_error) return send_error;
    assert(sent_count < sizeof(sent));
    sent[sent_count++] = req->cmd_id;
    client_busy = true; /* Busy until the simulated board response arrives. */
    return 0;
}
static void reset(void)
{
    assert(mist_service_init() == 0);
    sent_count = 0;
    client_busy = false;
    send_error = 0;
}
static void test_start_waits_for_set_level_reply(void)
{
    reset();
    assert(mist_service_set_desired(true, MIST_LEVEL_UI_MID) == 0);
    mist_service_process_tx();
    assert(sent_count == 1 && sent[0] == MIST_CMD_SET_LEVEL);
    for (unsigned i = 0; i < 10; ++i) {
        assert(mist_service_set_desired(true, MIST_LEVEL_UI_MID) == 0);
        mist_service_process_tx();
    }
    assert(sent_count == 1);
    client_busy = false;
    mist_service_process_tx();
    assert(sent_count == 2 && sent[1] == MIST_CMD_START);
    client_busy = false;
    mist_service_process_tx();
    assert(sent_count == 2); /* No duplicate START from repeated control steps. */
    assert(mist_service_request_stop() == 0);
    client_busy = true;
    mist_service_process_tx();
    client_busy = false;
    mist_service_process_tx();
    assert(sent_count == 3 && sent[2] == MIST_CMD_STOP);
}
static void test_status_poll_and_uart_error_do_not_drop_start(void)
{
    reset();
    assert(mist_service_queue(MIST_CMD_GET_STATUS, NULL, 0) == 0);
    mist_service_process_tx();
    assert(mist_service_set_desired(true, MIST_LEVEL_UI_LOW) == 0);
    mist_service_process_tx();
    assert(sent_count == 1);
    client_busy = false;
    send_error = -ETIMEDOUT;
    mist_service_process_tx();
    assert(sent_count == 1);
    send_error = 0;
    mist_service_process_tx();
    assert(sent_count == 2 && sent[1] == MIST_CMD_SET_LEVEL);
    client_busy = false;
    mist_service_process_tx();
    assert(sent_count == 3 && sent[2] == MIST_CMD_START);
}
static void test_full_queue_retries_without_reordering(void)
{
    reset();
    for (unsigned i = 0; i < APP_MIST_TX_QUEUE_LEN; ++i)
        assert(mist_service_queue(MIST_CMD_GET_STATUS, NULL, 0) == 0);
    assert(mist_service_set_desired(true, MIST_LEVEL_UI_HIGH) == -EAGAIN);
    /* Free only one slot: SET_LEVEL fits, START must be retried later. */
    mist_service_process_tx();
    assert(mist_service_set_desired(true, MIST_LEVEL_UI_HIGH) == -EAGAIN);
    for (unsigned i = 1; i < APP_MIST_TX_QUEUE_LEN; ++i) {
        client_busy = false;
        mist_service_process_tx();
    }
    assert(mist_service_set_desired(true, MIST_LEVEL_UI_HIGH) == 0);
    client_busy = false;
    mist_service_process_tx();
    assert(sent[sent_count - 1] == MIST_CMD_SET_LEVEL);
    client_busy = false;
    mist_service_process_tx();
    assert(sent[sent_count - 1] == MIST_CMD_START);
}
int main(void)
{
    test_start_waits_for_set_level_reply();
    test_status_poll_and_uart_error_do_not_drop_start();
    test_full_queue_retries_without_reordering();
    puts("test_mist_service: PASS (busy, delayed reply, UART error, full queue, STOP)");
    return 0;
}
