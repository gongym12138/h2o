/*
 * Copyright (c) 2017 Ritta Narita
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <mruby.h>
#include <mruby/array.h>
#include <mruby/error.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include "h2o/mruby_.h"
#include "embedded.c.h"

struct st_h2o_mruby_channel_context_t {
    h2o_mruby_context_t *ctx;
    mrb_value receiver;
    mrb_value channel;
};

static void attach_receiver(struct st_h2o_mruby_channel_context_t *ctx, mrb_value receiver)
{
    assert(mrb_nil_p(ctx->receiver));
    ctx->receiver = receiver;
    mrb_gc_register(ctx->ctx->shared->mrb, receiver);
}

static mrb_value detach_receiver(struct st_h2o_mruby_channel_context_t *ctx)
{
    mrb_value ret = ctx->receiver;
    assert(!mrb_nil_p(ret));
    ctx->receiver = mrb_nil_value();
    mrb_gc_unregister(ctx->ctx->shared->mrb, ret);
    mrb_gc_protect(ctx->ctx->shared->mrb, ret);
    return ret;
}

static void on_gc_dispose_channel(mrb_state *mrb, void *_ctx)
{
    struct st_h2o_mruby_channel_context_t *ctx = _ctx;
    if (ctx != NULL)
        ctx->channel = mrb_nil_value();
}

const static struct mrb_data_type channel_type = {"channel", on_gc_dispose_channel};

static mrb_value channel_initialize_method(mrb_state *mrb, mrb_value self)
{
    h2o_mruby_shared_context_t *shared_ctx = mrb->ud;

    struct st_h2o_mruby_channel_context_t *ctx;
    ctx = h2o_mem_alloc(sizeof(*ctx));

    memset(ctx, 0, sizeof(*ctx));
    ctx->ctx = shared_ctx->current_context;
    ctx->receiver = mrb_nil_value();

    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@queue"), mrb_ary_new(mrb));

    DATA_PTR(self) = ctx;
    DATA_TYPE(self) = &channel_type;

    return self;
}

static mrb_value channel_notify_method(mrb_state *mrb, mrb_value self)
{
    struct st_h2o_mruby_channel_context_t *ctx;
    ctx = DATA_PTR(self);

    if (!mrb_nil_p(ctx->receiver)) {
        h2o_mruby_run_fiber(ctx->ctx, detach_receiver(ctx), mrb_nil_value(), NULL);
        h2o_mruby_shared_context_t *shared_ctx = mrb->ud;
        /* When it's called in task, retrieve current_context for next action in task */
        shared_ctx->current_context = ctx->ctx;
    }

    return mrb_nil_value();
}

mrb_value h2o_mruby_channel_shift_callback(h2o_mruby_context_t *mctx, mrb_value receiver, mrb_value args,
                                                int *run_again)
{
    mrb_state *mrb = mctx->shared->mrb;

    struct st_h2o_mruby_channel_context_t *ctx;

    if ((ctx = DATA_PTR(mrb_ary_entry(args, 0))) == NULL)
        return mrb_exc_new_str_lit(mrb, E_ARGUMENT_ERROR, "Channel#shift wrong self");

    attach_receiver(ctx, receiver);

    return mrb_nil_value();
}

void h2o_mruby_channel_init_context(h2o_mruby_shared_context_t *shared_ctx)
{
    mrb_state *mrb = shared_ctx->mrb;

    h2o_mruby_eval_expr(mrb, H2O_MRUBY_CODE_CHANNEL);
    h2o_mruby_assert(mrb);

    struct RClass *module, *klass;
    module = mrb_define_module(mrb, "H2O");

    klass = mrb_class_get_under(mrb, module, "Channel");
    mrb_ary_set(mrb, shared_ctx->constants, H2O_MRUBY_CHANNEL_CLASS, mrb_obj_value(klass));
    mrb_define_method(mrb, klass, "initialize", channel_initialize_method, MRB_ARGS_NONE());
    mrb_define_method(mrb, klass, "_notify", channel_notify_method, MRB_ARGS_NONE());
    h2o_mruby_define_callback(mrb, "_h2o__channel_wait", H2O_MRUBY_CALLBACK_ID_CHANNEL_WAIT);
}