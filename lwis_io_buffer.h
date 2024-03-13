/*
 * Google LWIS Buffer I/O Implementation
 *
 * Copyright (c) 2024 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef LWIS_IO_BUFFER_H_
#define LWIS_IO_BUFFER_H_

#include "lwis_commands.h"
#include "lwis_device.h"

/*
 * lwis_io_buffer_write:
 * write the byte to the buffer.
 */
int lwis_io_buffer_write(struct lwis_device *lwis_dev, struct lwis_io_entry *entry);

#endif /* LWIS_IO_BUFFER_H_ */