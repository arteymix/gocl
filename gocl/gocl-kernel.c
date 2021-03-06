/*
 * gocl-kernel.c
 *
 * Gocl - GLib/GObject wrapper for OpenCL
 * Copyright (C) 2012-2013 Igalia S.L.
 *
 * Authors:
 *  Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

/**
 * SECTION:gocl-kernel
 * @short_description: Object that represents an OpenCL kernel
 * @stability: Unstable
 *
 * A #GoclKernel object represents a special type of functions in OpenCL
 * programs, that allows for host code to set its arguments and invoke the
 * function.
 *
 * A #GoclKernel is not created directly. Instead, it is obtained from a
 * #GoclProgram by calling gocl_program_get_kernel() method.
 *
 * Before a kernel object can be executed, all its arguments must be set.
 * Several methods are provided for this purpose, and depending on the type
 * of the argument to set, one or other is used.
 * gocl_kernel_set_argument(), gocl_kernel_set_argument_int32() and
 * gocl_kernel_set_argument_buffer() are examples of such methods.
 * More will be added soon.
 *
 * Once all arguments are set, the kernel is ready to be executed on a device.
 * For this, the gocl_kernel_run_in_device() is used for non-blocking execution,
 * and gocl_kernel_run_in_device_sync() for a blocking version. Notice that
 * these methods will use the device's default command queue. In the future,
 * methods will be provided to run the kernel on arbitrary command queues
 * as well.
 **/

/**
 * GoclKernelClass:
 * @parent_class: The parent class
 *
 * The class for #GoclKernel objects.
 **/

#include <string.h>
#include <gio/gio.h>

#include "gocl-kernel.h"

#include "gocl-private.h"
#include "gocl-program.h"

typedef gsize WorkSize[3];

struct _GoclKernelPrivate
{
  cl_kernel kernel;
  gchar *name;

  GoclProgram *program;

  WorkSize global_work_size;
  WorkSize local_work_size;
  guint8 work_dim;
};

/* properties */
enum
{
  PROP_0,
  PROP_PROGRAM,
  PROP_NAME
};

static void           gocl_kernel_class_init            (GoclKernelClass *class);
static void           gocl_kernel_initable_iface_init   (GInitableIface *iface);
static gboolean       gocl_kernel_initable_init         (GInitable     *initable,
                                                         GCancellable  *cancellable,
                                                         GError       **error);
static void           gocl_kernel_init                  (GoclKernel *self);
static void           gocl_kernel_finalize              (GObject *obj);

static void           set_property                       (GObject      *obj,
                                                          guint         prop_id,
                                                          const GValue *value,
                                                          GParamSpec   *pspec);
static void           get_property                       (GObject    *obj,
                                                          guint       prop_id,
                                                          GValue     *value,
                                                          GParamSpec *pspec);

G_DEFINE_TYPE_WITH_CODE (GoclKernel, gocl_kernel, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                gocl_kernel_initable_iface_init));

#define GOCL_KERNEL_GET_PRIVATE(obj)                    \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj),                  \
                                GOCL_TYPE_KERNEL,       \
                                GoclKernelPrivate))     \

static void
gocl_kernel_class_init (GoclKernelClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = gocl_kernel_finalize;
  obj_class->get_property = get_property;
  obj_class->set_property = set_property;

  g_object_class_install_property (obj_class, PROP_PROGRAM,
                                   g_param_spec_object ("program",
                                                        "Program",
                                                        "The program owning this kernel",
                                                        GOCL_TYPE_PROGRAM,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Kernel name",
                                                        "The name of the kernel function",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (class, sizeof (GoclKernelPrivate));
}

static void
gocl_kernel_initable_iface_init (GInitableIface *iface)
{
  iface->init = gocl_kernel_initable_init;
}

static gboolean
gocl_kernel_initable_init (GInitable     *initable,
                           GCancellable  *cancellable,
                           GError       **error)
{
  GoclKernel *self = GOCL_KERNEL (initable);
  cl_program program;
  cl_int err_code = 0;

  program = gocl_program_get_program (self->priv->program);

  self->priv->kernel = clCreateKernel (program, self->priv->name, &err_code);
  if (gocl_error_check_opencl (err_code, error))
    return FALSE;

  return TRUE;
}

static void
gocl_kernel_init (GoclKernel *self)
{
  GoclKernelPrivate *priv;

  self->priv = priv = GOCL_KERNEL_GET_PRIVATE (self);

  priv->work_dim = 1;
  memset (&priv->global_work_size, 0, 3);
  memset (&priv->local_work_size, 0, 3);
}

static void
gocl_kernel_finalize (GObject *obj)
{
  GoclKernel *self = GOCL_KERNEL (obj);

  g_free (self->priv->name);

  g_object_unref (self->priv->program);

  clReleaseKernel (self->priv->kernel);

  G_OBJECT_CLASS (gocl_kernel_parent_class)->finalize (obj);
}

static void
set_property (GObject      *obj,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  GoclKernel *self;

  self = GOCL_KERNEL (obj);

  switch (prop_id)
    {
    case PROP_PROGRAM:
      self->priv->program = g_value_dup_object (value);
      break;

    case PROP_NAME:
      self->priv->name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
get_property (GObject    *obj,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
  GoclKernel *self;

  self = GOCL_KERNEL (obj);

  switch (prop_id)
    {
    case PROP_PROGRAM:
      g_value_set_object (value, self->priv->program);
      break;

    case PROP_NAME:
      g_value_set_string (value, self->priv->name);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

/* public */

/**
 * gocl_kernel_get_kernel:
 * @self: The #GoclKernel
 *
 * Retrieves the internal OpenCL #cl_kernel object. This is not normally
 * called by applications. It is rather a low-level, internal API.
 *
 * Returns: (transfer none) (type guint64): The internal #cl_kernel object
 **/
cl_kernel
gocl_kernel_get_kernel (GoclKernel *self)
{
  g_return_val_if_fail (GOCL_IS_KERNEL (self), NULL);

  return self->priv->kernel;
}

/**
 * gocl_kernel_set_argument:
 * @self: The #GoclKernel
 * @index: The index of this argument in the kernel function
 * @size: The size of @buffer, in bytes
 * @buffer: A pointer to an arbitrary block of memory
 *
 * Sets the value of the kernel argument at @index, as an arbitrary block of
 * memory.
 *
 * Returns: %TRUE on success, %FALSE on error
 **/
gboolean
gocl_kernel_set_argument (GoclKernel      *self,
                          guint            index,
                          gsize            size,
                          const gpointer  *buffer)
{
  cl_int err_code;

  g_return_val_if_fail (GOCL_IS_KERNEL (self), FALSE);

  err_code = clSetKernelArg (self->priv->kernel,
                             index,
                             size,
                             buffer);

  return ! gocl_error_check_opencl_internal (err_code);
}

/**
 * gocl_kernel_set_argument_int32:
 * @self: The #GoclKernel
 * @index: The index of this argument in the kernel function
 * @num_elements: The number of int32 elements in @buffer
 * @buffer: (array length=num_elements) (element-type guint32): Array of int32
 * values
 *
 * Sets the value of the kernel argument at @index, as an array of int32.
 *
 * Returns: %TRUE on success, %FALSE on error
 **/
gboolean
gocl_kernel_set_argument_int32 (GoclKernel  *self,
                                guint        index,
                                gsize        num_elements,
                                gint32      *buffer)
{
  return gocl_kernel_set_argument (self,
                                   index,
                                   sizeof (cl_uint) * num_elements,
                                   (const gpointer) buffer);
}

/**
 * gocl_kernel_set_argument_float:
 * @self: The #GoclKernel
 * @index: The index of this argument in the kernel function
 * @num_elements: The number of float elements in @buffer
 * @buffer: (array length=num_elements) (element-type gfloat): Array of float
 * values
 *
 * Sets the value of the kernel argument at @index, as an array of floats.
 *
 * Returns: %TRUE on success, %FALSE on error
 **/
gboolean
gocl_kernel_set_argument_float (GoclKernel  *self,
                                guint        index,
                                gsize        num_elements,
                                gfloat      *buffer)
{
  return gocl_kernel_set_argument (self,
                                   index,
                                   sizeof (cl_float) * num_elements,
                                   (const gpointer) buffer);
}

/**
 * gocl_kernel_set_argument_buffer:
 * @self: The #GoclKernel
 * @index: The index of this argument in the kernel function
 * @buffer: A #GoclBuffer
 *
 * Sets the value of the kernel argument at @index, as a buffer object.
 *
 * Returns: %TRUE on success, %FALSE on error
 **/
gboolean
gocl_kernel_set_argument_buffer (GoclKernel  *self,
                                 guint        index,
                                 GoclBuffer  *buffer)
{
  cl_int err_code;
  cl_mem buf;

  g_return_val_if_fail (GOCL_IS_KERNEL (self), FALSE);

  buf = gocl_buffer_get_buffer (buffer);

  err_code = clSetKernelArg (self->priv->kernel,
                             index,
                             sizeof (cl_mem),
                             &buf);

  return ! gocl_error_check_opencl_internal (err_code);
}

/**
 * gocl_kernel_run_in_device_sync:
 * @self: The #GoclKernel
 * @device: A #GoclDevice to run the kernel on
 * @event_wait_list: (element-type Gocl.Event) (allow-none): List of #GoclEvent
 * events to wait for, or %NULL
 *
 * Runs the kernel on the specified device, blocking the program
 * until the kernel execution finishes. For non-blocking version,
 * gocl_kernel_run_in_device() is provided.
 *
 * If @event_wait_list is provided, the kernel execution will start
 * only when all the events in the list have triggered.
 *
 * Returns: %TRUE on success, %FALSE on error
 **/
gboolean
gocl_kernel_run_in_device_sync (GoclKernel  *self,
                                GoclDevice  *device,
                                GList       *event_wait_list)
{
  cl_int err_code;
  cl_event event;
  GoclQueue *queue;
  cl_command_queue _queue;
  cl_event *_event_wait_list = NULL;

  g_return_val_if_fail (GOCL_IS_KERNEL (self), FALSE);
  g_return_val_if_fail (GOCL_IS_DEVICE (device), FALSE);

  queue = gocl_device_get_default_queue (device);
  if (queue == NULL)
    return FALSE;

  _event_wait_list = gocl_event_list_to_array (event_wait_list, NULL);

  _queue = gocl_queue_get_queue (queue);

  err_code =
    clEnqueueNDRangeKernel (_queue,
                            self->priv->kernel,
                            self->priv->work_dim,
                            NULL,
                            self->priv->global_work_size[0] == 0 ?
                              NULL : (gsize *) &self->priv->global_work_size,
                            self->priv->local_work_size[0] == 0 ?
                              NULL : (gsize *) &self->priv->local_work_size,
                            g_list_length (event_wait_list),
                            _event_wait_list,
                            &event);
  g_free (_event_wait_list);

  if (gocl_error_check_opencl_internal (err_code))
    return FALSE;

  clWaitForEvents (1, &event);
  clReleaseEvent (event);

  return TRUE;
}

/**
 * gocl_kernel_run_in_device:
 * @self: The #GoclKernel
 * @device: A #GoclDevice to run the kernel on
 * @event_wait_list: (element-type Gocl.Event) (allow-none): List of #GoclEvent
 * events to wait for, or %NULL
 *
 * Runs the kernel on the specified device, asynchronously. A #GoclEvent
 * is returned, and can be used to get notified when the execution finishes,
 * or as wait event input to other operations on the device.
 *
 * If @event_wait_list is provided, the kernel execution will start
 * only when all the events in the list have triggered.
 *
 * Returns: (transfer none): A #GoclEvent to get notified when execution
 * finishes
 **/
GoclEvent *
gocl_kernel_run_in_device (GoclKernel *self,
                           GoclDevice *device,
                           GList      *event_wait_list)
{
  GError *error = NULL;

  cl_int err_code;
  cl_event event;
  GoclQueue *queue = NULL;
  cl_command_queue _queue;

  GoclEvent *_event = NULL;
  GoclEventResolverFunc resolver_func;

  cl_event *_event_wait_list = NULL;
  guint event_wait_list_len;

  g_return_val_if_fail (GOCL_IS_KERNEL (self), FALSE);
  g_return_val_if_fail (GOCL_IS_DEVICE (device), FALSE);

  _event_wait_list = gocl_event_list_to_array (event_wait_list,
                                               &event_wait_list_len);

  queue = gocl_device_get_default_queue (device);
  if (queue == NULL)
    goto out;

  _queue = gocl_queue_get_queue (queue);

  err_code =
    clEnqueueNDRangeKernel (_queue,
                            self->priv->kernel,
                            self->priv->work_dim,
                            NULL,
                            self->priv->global_work_size[0] == 0 ?
                              NULL : (gsize *) &self->priv->global_work_size,
                            self->priv->local_work_size[0] == 0 ?
                              NULL : (gsize *) &self->priv->local_work_size,
                            event_wait_list_len,
                            _event_wait_list,
                            &event);
  g_free (_event_wait_list);

  if (! gocl_error_check_opencl (err_code, &error))
    {
      _event = g_object_new (GOCL_TYPE_EVENT,
                             "queue", queue,
                             "event", event,
                             NULL);
      gocl_event_set_event_wait_list (_event, event_wait_list);
      gocl_event_steal_resolver_func (_event);
    }

 out:
  if (error != NULL)
    {
      _event = g_object_new (GOCL_TYPE_EVENT,
                             "queue", queue,
                             NULL);
      resolver_func = gocl_event_steal_resolver_func (_event);
      resolver_func (_event, error);
      g_error_free (error);
    }

  gocl_event_idle_unref (_event);

  return _event;
}

/**
 * gocl_kernel_set_work_dimension:
 * @self: The #GoclKernel
 * @work_dim: The work dimension
 *
 * Sets the work dimension (1, 2, 3) to use when executing the kernel.
 **/
void
gocl_kernel_set_work_dimension (GoclKernel *self, guint8 work_dim)
{
  g_return_if_fail (GOCL_IS_KERNEL (self));
  g_return_if_fail (work_dim > 0 && work_dim <= 3);

  self->priv->work_dim = work_dim;
}

/**
 * gocl_kernel_set_global_work_size:
 * @self: The #GoclKernel
 * @size1: global work size for the first dimension
 * @size2: global work size for the second dimension
 * @size3: global work size for the third dimension
 *
 * Sets the global work sizes to use when executing the kernel, corresponding
 * to the first, second, and third dimensions, respectively. By default, the
 * sizes are all zeros.
 *
 * If the @size1 value is zero, it means no global work size is specified and
 * %NULL will be used when enqueuing the kernel.
 **/
void
gocl_kernel_set_global_work_size (GoclKernel *self,
                                  gsize       size1,
                                  gsize       size2,
                                  gsize       size3)
{
  g_return_if_fail (GOCL_IS_KERNEL (self));

  self->priv->global_work_size[0] = size1;
  self->priv->global_work_size[1] = size2;
  self->priv->global_work_size[2] = size3;
}

/**
 * gocl_kernel_set_local_work_size:
 * @self: The #GoclKernel
 * @size1: local work size for the first dimension
 * @size2: local work size for the second dimension
 * @size3: local work size for the third dimension
 *
 * Sets the local work sizes to use when executing the kernel, corresponding
 * to the first, second, and third dimensions, respectively. By default, the
 * sizes are all zeros.
 *
 * If the @size1 value is zero, it means no local work size is specified and
 * %NULL will be used when enqueuing the kernel.
 **/
void
gocl_kernel_set_local_work_size (GoclKernel *self,
                                 gsize       size1,
                                 gsize       size2,
                                 gsize       size3)
{
  g_return_if_fail (GOCL_IS_KERNEL (self));

  self->priv->local_work_size[0] = size1;
  self->priv->local_work_size[1] = size2;
  self->priv->local_work_size[2] = size3;
}
