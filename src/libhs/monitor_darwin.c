/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Niels Martignène <niels.martignene@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "common_priv.h"
#include <CoreFoundation/CFRunLoop.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include "device_priv.h"
#include "filter_priv.h"
#include "list.h"
#include "monitor_priv.h"
#include "platform.h"

struct hs_monitor {
    _HS_MONITOR

    IONotificationPortRef notify_port;
    int kqfd;
    mach_port_t port_set;
    bool started;

    io_iterator_t iterators[8];
    unsigned int iterator_count;
    int notify_ret;

    hs_enumerate_func *callback;
    void *callback_udata;
};

struct device_class {
    const char *old_stack;
    const char *new_stack;

    hs_device_type type;
};

struct usb_controller {
    _hs_list_head list;

    uint8_t index;
    io_string_t path;
};

struct service_aggregate {
    io_service_t dev_service;
    io_service_t iface_service;
    io_service_t usb_service;
};

extern const struct _hs_device_vtable _hs_posix_device_vtable;
extern const struct _hs_device_vtable _hs_darwin_hid_vtable;

static struct device_class device_classes[] = {
    {"IOHIDDevice",       "IOUSBHostHIDDevice", HS_DEVICE_TYPE_HID},
    {"IOSerialBSDClient", "IOSerialBSDClient",  HS_DEVICE_TYPE_SERIAL},
    {NULL}
};

static bool uses_new_stack()
{
    static bool init, new_stack;

    if (!init) {
        new_stack = hs_darwin_version() >= 150000;
        init = true;
    }

    return new_stack;
}

static const char *correct_class(const char *new_stack, const char *old_stack)
{
    return uses_new_stack() ? new_stack : old_stack;
}

static int get_ioregistry_value_string(io_service_t service, CFStringRef prop, char **rs)
{
    CFTypeRef data;
    CFIndex size;
    char *s;
    int r;

    data = IORegistryEntryCreateCFProperty(service, prop, kCFAllocatorDefault, 0);
    if (!data || CFGetTypeID(data) != CFStringGetTypeID()) {
        r = 0;
        goto cleanup;
    }

    size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(data), kCFStringEncodingUTF8) + 1;

    s = malloc((size_t)size);
    if (!s) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto cleanup;
    }

    r = CFStringGetCString(data, s, size, kCFStringEncodingUTF8);
    if (!r) {
        r = 0;
        goto cleanup;
    }

    *rs = s;
    r = 1;
cleanup:
    if (data)
        CFRelease(data);
    return r;
}

static bool get_ioregistry_value_number(io_service_t service, CFStringRef prop, CFNumberType type,
                                        void *rn)
{
    CFTypeRef data;
    bool r;

    data = IORegistryEntryCreateCFProperty(service, prop, kCFAllocatorDefault, 0);
    if (!data || CFGetTypeID(data) != CFNumberGetTypeID()) {
        r = false;
        goto cleanup;
    }

    r = CFNumberGetValue(data, type, rn);
cleanup:
    if (data)
        CFRelease(data);
    return r;
}

static int get_ioregistry_entry_path(io_service_t service, char **rpath)
{
    io_string_t buf;
    char *path;
    kern_return_t kret;

    kret = IORegistryEntryGetPath(service, kIOServicePlane, buf);
    if (kret != kIOReturnSuccess) {
        hs_log(HS_LOG_DEBUG, "IORegistryEntryGetPath() failed with code %d", kret);
        return 0;
    }

    path = strdup(buf);
    if (!path)
        return hs_error(HS_ERROR_MEMORY, NULL);

    *rpath = path;
    return 1;
}

static void clear_iterator(io_iterator_t it)
{
    io_object_t object;
    while ((object = IOIteratorNext(it)))
        IOObjectRelease(object);
}

static int find_device_node(struct service_aggregate *agg, hs_device *dev)
{
    int r;

    if (IOObjectConformsTo(agg->dev_service, "IOSerialBSDClient")) {
        dev->type = HS_DEVICE_TYPE_SERIAL;
        dev->vtable = &_hs_posix_device_vtable;

        r = get_ioregistry_value_string(agg->dev_service, CFSTR("IOCalloutDevice"), &dev->path);
        if (!r)
            hs_log(HS_LOG_WARNING, "Serial device does not have property 'IOCalloutDevice'");
    } else if (IOObjectConformsTo(agg->dev_service, "IOHIDDevice")) {
        dev->type = HS_DEVICE_TYPE_HID;
        dev->vtable = &_hs_darwin_hid_vtable;

        r = get_ioregistry_entry_path(agg->dev_service, &dev->path);
    } else {
        hs_log(HS_LOG_WARNING, "Cannot find device node for unknown device entry class");
        r = 0;
    }

    return r;
}

static int build_location_string(uint8_t ports[], unsigned int depth, char **rpath)
{
    char buf[256];
    char *ptr;
    size_t size;
    char *path;
    int r;

    ptr = buf;
    size = sizeof(buf);

    strcpy(buf, "usb");
    ptr += strlen(buf);
    size -= (size_t)(ptr - buf);

    for (unsigned int i = 0; i < depth; i++) {
        r = snprintf(ptr, size, "-%hhu", ports[i]);
        assert(r >= 2 && (size_t)r < size);

        ptr += r;
        size -= (size_t)r;
    }

    path = strdup(buf);
    if (!path)
        return hs_error(HS_ERROR_MEMORY, NULL);

    *rpath = path;
    return 0;
}

static io_service_t get_parent_and_release(io_service_t service, const io_name_t plane)
{
    io_service_t parent;
    kern_return_t kret;

    kret = IORegistryEntryGetParentEntry(service, plane, &parent);
    IOObjectRelease(service);
    if (kret != kIOReturnSuccess)
        return 0;

    return parent;
}

static int resolve_device_location(io_service_t usb_service, char **rlocation)
{
    uint32_t location_id;
    uint8_t ports[16];
    unsigned int depth;
    int r;

    r = get_ioregistry_value_number(usb_service, CFSTR("locationID"), kCFNumberSInt32Type,
                                    &location_id);
    if (!r) {
        hs_log(HS_LOG_WARNING, "Ignoring device without 'locationID' property");
        return 0;
    }

    ports[0] = location_id >> 24;
    for (depth = 0; depth <= 5 && ports[depth]; depth++)
        ports[depth + 1] = (location_id >> (20 - depth * 4)) & 0xF;

    r = build_location_string(ports, depth, rlocation);
    if (r < 0)
        return r;

    return 1;
}

static io_service_t find_conforming_parent(io_service_t service, const char *cls)
{
    IOObjectRetain(service);
    do {
        service = get_parent_and_release(service, kIOServicePlane);
    } while (service && !IOObjectConformsTo(service, cls));

    return service;
}

static int fill_device_details(struct service_aggregate *agg, hs_device *dev)
{
    uint64_t session;
    int r;

#define GET_MANDATORY_PROPERTY_NUMBER(service, key, type, var) \
        r = get_ioregistry_value_number((service), CFSTR(key), (type), (var)); \
        if (!r) { \
            hs_log(HS_LOG_WARNING, "Missing property '%s', ignoring device", (key)); \
            return 0; \
        }
#define GET_OPTIONAL_PROPERTY_STRING(service, key, var) \
        r = get_ioregistry_value_string((service), CFSTR(key), (var)); \
        if (r < 0) \
            return r;

    GET_MANDATORY_PROPERTY_NUMBER(agg->usb_service, "sessionID", kCFNumberSInt64Type, &session);
    GET_MANDATORY_PROPERTY_NUMBER(agg->usb_service, "idVendor", kCFNumberSInt64Type, &dev->vid);
    GET_MANDATORY_PROPERTY_NUMBER(agg->usb_service, "idProduct", kCFNumberSInt64Type, &dev->pid);
    GET_MANDATORY_PROPERTY_NUMBER(agg->iface_service, "bInterfaceNumber", kCFNumberSInt64Type,
                                  &dev->iface);

    GET_OPTIONAL_PROPERTY_STRING(agg->usb_service, "USB Vendor Name", &dev->manufacturer);
    GET_OPTIONAL_PROPERTY_STRING(agg->usb_service, "USB Product Name", &dev->product);
    GET_OPTIONAL_PROPERTY_STRING(agg->usb_service, "USB Serial Number", &dev->serial);

#undef GET_MANDATORY_PROPERTY_NUMBER
#undef GET_OPTIONAL_PROPERTY_STRING

    r = asprintf(&dev->key, "%"PRIx64, session);
    if (r < 0)
        return hs_error(HS_ERROR_MEMORY, NULL);

    return 1;
}

static void fill_hid_properties(struct service_aggregate *agg, hs_device *dev)
{
    bool success = true;

    success &= get_ioregistry_value_number(agg->dev_service, CFSTR("PrimaryUsagePage"),
                                           kCFNumberSInt16Type, &dev->u.hid.usage_page);
    success &= get_ioregistry_value_number(agg->dev_service, CFSTR("PrimaryUsage"),
                                           kCFNumberSInt16Type, &dev->u.hid.usage);

    if (!success)
        hs_log(HS_LOG_WARNING, "Invalid HID values for '%s", dev->path);
}

static int process_darwin_device(io_service_t service, hs_device **rdev)
{
    struct service_aggregate agg = {0};
    hs_device *dev = NULL;
    int r;

    agg.dev_service = service;
    agg.iface_service = find_conforming_parent(agg.dev_service, "IOUSBInterface");
    agg.usb_service = find_conforming_parent(agg.iface_service, "IOUSBDevice");
    if (!agg.iface_service || !agg.usb_service) {
        r = 0;
        goto cleanup;
    }

    dev = calloc(1, sizeof(*dev));
    if (!dev) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto cleanup;
    }
    dev->refcount = 1;
    dev->state = HS_DEVICE_STATUS_ONLINE;

    r = find_device_node(&agg, dev);
    if (r <= 0)
        goto cleanup;

    r = fill_device_details(&agg, dev);
    if (r <= 0)
        goto cleanup;

    if (dev->type == HS_DEVICE_TYPE_HID)
        fill_hid_properties(&agg, dev);

    r = resolve_device_location(agg.usb_service, &dev->location);
    if (r <= 0)
        goto cleanup;

    *rdev = dev;
    dev = NULL;
    r = 1;

cleanup:
    hs_device_unref(dev);
    if (agg.usb_service)
        IOObjectRelease(agg.usb_service);
    if (agg.iface_service)
        IOObjectRelease(agg.iface_service);
    return r;
}

static int process_iterator_devices(io_iterator_t it, const _hs_filter *filter,
                                    hs_enumerate_func *f, void *udata)
{
    io_service_t service;

    while ((service = IOIteratorNext(it))) {
        hs_device *dev;
        int r;

        r = process_darwin_device(service, &dev);
        IOObjectRelease(service);
        if (r < 0)
            return r;
        if (!r)
            continue;

        if (_hs_filter_match_device(filter, dev)) {
            r = (*f)(dev, udata);
            hs_device_unref(dev);
            if (r)
                return r;
        } else {
            hs_device_unref(dev);
        }
    }

    return 0;
}

static int attached_callback(hs_device *dev, void *udata)
{
    hs_monitor *monitor = udata;

    return _hs_monitor_add(monitor, dev, monitor->callback, monitor->callback_udata);
}

static void darwin_devices_attached(void *udata, io_iterator_t it)
{
    hs_monitor *monitor = udata;

    monitor->notify_ret = process_iterator_devices(it, &monitor->filter,
                                                   attached_callback, monitor);
}

static void darwin_devices_detached(void *udata, io_iterator_t it)
{
    hs_monitor *monitor = udata;

    io_service_t service;
    while ((service = IOIteratorNext(it))) {
        uint64_t session;
        int r;

        r = get_ioregistry_value_number(service, CFSTR("sessionID"), kCFNumberSInt64Type, &session);
        if (r) {
            char key[32];

            sprintf(key, "%"PRIx64, session);
            _hs_monitor_remove(monitor, key, monitor->callback, monitor->callback_udata);
        }

        IOObjectRelease(service);
    }
}

struct enumerate_enumerate_context {
    hs_enumerate_func *f;
    void *udata;
};

static int enumerate_enumerate_callback(hs_device *dev, void *udata)
{
    struct enumerate_enumerate_context *ctx = udata;

    _hs_device_log(dev, "Enumerate");
    return (*ctx->f)(dev, ctx->udata);
}

int hs_enumerate(const hs_match *matches, unsigned int count, hs_enumerate_func *f, void *udata)
{
    assert(f);

    _hs_filter filter;
    struct enumerate_enumerate_context ctx;
    io_iterator_t it = 0;
    kern_return_t kret;
    int r;

    r = _hs_filter_init(&filter, matches, count);
    if (r < 0)
        goto cleanup;

    ctx.f = f;
    ctx.udata = udata;

    for (unsigned int i = 0; device_classes[i].old_stack; i++) {
        if (_hs_filter_has_type(&filter, device_classes[i].type)) {
            const char *cls = correct_class(device_classes[i].new_stack, device_classes[i].old_stack);

            kret = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching(cls), &it);
            if (kret != kIOReturnSuccess) {
                r = hs_error(HS_ERROR_SYSTEM, "IOServiceGetMatchingServices('%s') failed", cls);
                goto cleanup;
            }

            r = process_iterator_devices(it, &filter, enumerate_enumerate_callback, &ctx);
            if (r)
                goto cleanup;

            IOObjectRelease(it);
            it = 0;
        }
    }

    r = 0;
cleanup:
    if (it) {
        clear_iterator(it);
        IOObjectRelease(it);
    }
    _hs_filter_release(&filter);
    return r;
}

static int add_notification(hs_monitor *monitor, const char *cls, const io_name_t type,
                            IOServiceMatchingCallback f, io_iterator_t *rit)
{
    io_iterator_t it;
    kern_return_t kret;

    kret = IOServiceAddMatchingNotification(monitor->notify_port, type, IOServiceMatching(cls),
                                            f, monitor, &it);
    if (kret != kIOReturnSuccess)
        return hs_error(HS_ERROR_SYSTEM, "IOServiceAddMatchingNotification('%s') failed", cls);

    assert(monitor->iterator_count < _HS_COUNTOF(monitor->iterators));
    monitor->iterators[monitor->iterator_count++] = it;
    *rit = it;

    return 0;
}

int hs_monitor_new(const hs_match *matches, unsigned int count, hs_monitor **rmonitor)
{
    assert(rmonitor);

    hs_monitor *monitor = NULL;
    struct kevent kev;
    const struct timespec ts = {0};
    kern_return_t kret;
    int r;

    monitor = calloc(1, sizeof(*monitor));
    if (!monitor) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto error;
    }

    monitor->kqfd = -1;

    r = _hs_monitor_init(monitor, matches, count);
    if (r < 0)
        goto error;

    monitor->notify_port = IONotificationPortCreate(kIOMasterPortDefault);
    if (!monitor->notify_port) {
        r = hs_error(HS_ERROR_SYSTEM, "IONotificationPortCreate() failed");
        goto error;
    }

    monitor->kqfd = kqueue();
    if (monitor->kqfd < 0) {
        r = hs_error(HS_ERROR_SYSTEM, "kqueue() failed: %s", strerror(errno));
        goto error;
    }

    kret = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &monitor->port_set);
    if (kret != KERN_SUCCESS) {
        r = hs_error(HS_ERROR_SYSTEM, "mach_port_allocate() failed");
        goto error;
    }

    kret = mach_port_insert_member(mach_task_self(), IONotificationPortGetMachPort(monitor->notify_port),
                                   monitor->port_set);
    if (kret != KERN_SUCCESS) {
        r = hs_error(HS_ERROR_SYSTEM, "mach_port_insert_member() failed");
        goto error;
    }

    EV_SET(&kev, monitor->port_set, EVFILT_MACHPORT, EV_ADD, 0, 0, NULL);

    r = kevent(monitor->kqfd, &kev, 1, NULL, 0, &ts);
    if (r < 0) {
        r = hs_error(HS_ERROR_SYSTEM, "kevent() failed: %d", errno);
        goto error;
    }

    *rmonitor = monitor;
    return 0;

error:
    hs_monitor_free(monitor);
    return r;
}

void hs_monitor_free(hs_monitor *monitor)
{
    if (monitor) {
        for (unsigned int i = 0; i < monitor->iterator_count; i++) {
            clear_iterator(monitor->iterators[i]);
            IOObjectRelease(monitor->iterators[i]);
        }

        if (monitor->port_set)
            mach_port_deallocate(mach_task_self(), monitor->port_set);
        if (monitor->notify_port)
            IONotificationPortDestroy(monitor->notify_port);
        close(monitor->kqfd);

        _hs_monitor_release(monitor);
    }

    free(monitor);
}

hs_descriptor hs_monitor_get_descriptor(const hs_monitor *monitor)
{
    assert(monitor);
    return monitor->kqfd;
}

static int start_enumerate_callback(hs_device *dev, void *udata)
{
    return _hs_monitor_add(udata, dev, NULL, NULL);
}

int hs_monitor_start(hs_monitor *monitor)
{
    assert(monitor);

    io_iterator_t it;
    int r;

    if (monitor->started)
        return 0;

    for (unsigned int i = 0; device_classes[i].old_stack; i++) {
        if (_hs_filter_has_type(&monitor->filter, device_classes[i].type)) {
            r = add_notification(monitor, correct_class(device_classes[i].new_stack, device_classes[i].old_stack),
                                 kIOFirstMatchNotification, darwin_devices_attached, &it);
            if (r < 0)
                goto error;

            r = process_iterator_devices(it, &monitor->filter, start_enumerate_callback, monitor);
            if (r < 0)
                goto error;
        }
    }

    r = add_notification(monitor, correct_class("IOUSBHostDevice", kIOUSBDeviceClassName),
                         kIOTerminatedNotification, darwin_devices_detached, &it);
    if (r < 0)
        goto error;
    clear_iterator(it);

    monitor->started = true;
    return 0;

error:
    hs_monitor_stop(monitor);
    return r;
}

void hs_monitor_stop(hs_monitor *monitor)
{
    assert(monitor);

    if (!monitor->started)
        return;

    _hs_monitor_clear(monitor);
    for (unsigned int i = 0; i < monitor->iterator_count; i++) {
        clear_iterator(monitor->iterators[i]);
        IOObjectRelease(monitor->iterators[i]);
    }
    monitor->iterator_count = 0;

    monitor->started = false;
}

int hs_monitor_refresh(hs_monitor *monitor, hs_enumerate_func *f, void *udata)
{
    assert(monitor);

    struct kevent kev;
    const struct timespec ts = {0};
    int r;

    if (!monitor->started)
        return 0;

    r = kevent(monitor->kqfd, NULL, 0, &kev, 1, &ts);
    if (r < 0)
        return hs_error(HS_ERROR_SYSTEM, "kevent() failed: %s", strerror(errno));
    if (!r)
        return 0;
    assert(kev.filter == EVFILT_MACHPORT);

    monitor->callback = f;
    monitor->callback_udata = udata;

    r = 0;
    while (true) {
        struct {
            mach_msg_header_t header;
            uint8_t body[128];
        } msg;
        mach_msg_return_t mret;

        mret = mach_msg(&msg.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(msg),
                        monitor->port_set, 0, MACH_PORT_NULL);
        if (mret != MACH_MSG_SUCCESS) {
            if (mret == MACH_RCV_TIMED_OUT)
                break;

            r = hs_error(HS_ERROR_SYSTEM, "mach_msg() failed");
            break;
        }

        IODispatchCalloutFromMessage(NULL, &msg.header, monitor->notify_port);

        if (monitor->notify_ret) {
            r = monitor->notify_ret;
            monitor->notify_ret = 0;

            break;
        }
    }

    return r;
}