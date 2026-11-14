#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Dialogs: questions, forms and text fields as kettle components on the
 * modal stack (app->dialogs), drawn centred over the frame with the view
 * dimmed behind them. Each waits inside the handler that asked while the
 * loop keeps terminal clients and bounded observations moving; input goes
 * to the top dialog only. Paste is text; Esc and Ctrl-C cancel and nothing
 * is submitted; a question's default button is Cancel, so Enter alone never
 * accepts one. */

enum { DIALOG_QUESTION = 1, DIALOG_FORM };

static void dialogs_ready(struct app *app) {
    if (app->dialogs_ready) return;
    kt_modals_init(&app->dialogs);
    app->dialogs_ready = true;
}

/* Draws the stack over the frame being drawn. */
void dialogs_draw(struct app *app) {
    struct kt_rect whole;
    if (!app->dialogs_ready || !app->dialogs.count || !app->frame.cells) return;
    whole = (struct kt_rect){0, 0, app->frame.width, app->frame.height};
    kt_modals_place(&app->dialogs, whole);
    kt_modals_view(&app->dialogs, &app->frame, whole);
}

static void dialog_overlay(struct app *app, void *context) { (void)context; dialogs_draw(app); }

/* Runs the dialog on top of the stack until it has a final result: that
 * result, or -1 when it was closed, cancelled or the program is ending. */
/* One input message for the top dialog; false once the dialog is over. */
static bool dialog_input(struct app *app, struct kt_msg *msg, dialog_final_fn final, int *result) {
    if (msg->type == KT_MSG_KEY && kt_event_is(&msg->u.event, 'c', KT_MOD_CTRL)) return false;
    /* A line feed is Return, as scripts and some terminals send it. */
    if (msg->type == KT_MSG_KEY && kt_event_is(&msg->u.event, 'j', KT_MOD_CTRL)) {
        msg->u.event.key = KT_KEY_ENTER; msg->u.event.mods = 0;
    }
    switch (kt_modals_update(&app->dialogs, msg, app->cmds)) {
    case KT_MODALS_CLOSED: return false;
    case KT_MODALS_RESULT:
        if (!final(app->dialogs.result)) return true;
        *result = app->dialogs.result;
        return false;
    default: return true;
    }
}

int dialog_run(struct app *app, const struct kt_modal *dialog, dialog_final_fn final) {
    struct modal hook = {NULL, dialog_overlay, NULL};
    int result = -1;
    bool open = true;
    dialogs_ready(app);
    if (!kt_modals_push(&app->dialogs, dialog)) return -1;
    if (!modal_push(app, &hook)) { kt_modals_pop(&app->dialogs); return -1; }
    while (open) {
        struct kt_msg msg;
        enum modal_event event = modal_wait(app, &msg);
        if (event == MODAL_STOPPED) break;
        if (event == MODAL_INPUT) open = dialog_input(app, &msg, final, &result);
    }
    kt_modals_pop(&app->dialogs);
    modal_pop(app);
    return result;
}

/* ---- questions ----------------------------------------------------------- */

static bool question_final(int result) {
    return result == KT_CONFIRM_ACCEPTED || result == KT_CONFIRM_CANCELLED;
}

bool dialog_confirm(struct app *app, const char *title, const char *body, const char *accept, bool destructive) {
    struct kt_confirm question;
    struct kt_modal modal;
    int width = app->cols - 4 < 66 ? app->cols - 4 : 66;
    kt_confirm_init(&question, title, body, accept, "Cancel", destructive);
    question.focus = question.cancel;
    modal = kt_confirm_modal(&question, DIALOG_QUESTION, width < 30 ? 30 : width);
    return dialog_run(app, &modal, question_final) == KT_CONFIRM_ACCEPTED;
}

/* ---- forms --------------------------------------------------------------- */

static bool form_final(int result) {
    return result == KT_FORM_SUBMITTED || result == KT_FORM_CANCELLED;
}

int dialog_form_body(struct app *app, const char *title, const char *body, struct kt_field *fields, size_t count) {
    struct kt_form form;
    struct kt_modal modal;
    int width = app->cols - 4 < 70 ? app->cols - 4 : 70;
    kt_form_init(&form, title, fields, count);
    form.body = body;
    modal = kt_form_modal(&form, DIALOG_FORM, width < 30 ? 30 : width);
    return dialog_run(app, &modal, form_final) == KT_FORM_SUBMITTED ? 0 : -1;
}

/* One text field. A prompt such as "Search heads: " is the dialog's title;
 * a long one becomes the form's body under a short title. */
int prompt_text(struct app *app, const char *prompt, char *buffer, size_t size) {
    struct kt_textinput input;
    struct kt_field field;
    char title[96];
    size_t length = strlen(prompt);
    bool long_prompt;
    if (!size) return -1;
    buffer[0] = '\0';
    while (length && (prompt[length - 1] == ' ' || prompt[length - 1] == ':')) length--;
    long_prompt = length > 48;
    snprintf(title, sizeof(title), "%.*s", long_prompt ? 5 : (int)length, long_prompt ? "Input" : prompt);
    kt_textinput_init(&input, buffer, size);
    memset(&field, 0, sizeof(field));
    field.kind = KT_FIELD_TEXT; field.input = &input;
    return dialog_form_body(app, title, long_prompt ? prompt : NULL, &field, 1);
}
