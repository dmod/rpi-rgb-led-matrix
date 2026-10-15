from libc.stdint cimport uint8_t, uint32_t
cimport cython


@cython.boundscheck(False)
@cython.wraparound(False)
cdef void set_pixels_rgb(cppinc.Canvas* canvas, int xstart, int ystart, int width, int height,
                         const uint8_t[::1] rgb) noexcept nogil:
    # rgb holds packed rows of R, G, B bytes, as returned by Pillow's Image.tobytes() for mode "RGB".
    cdef int row, col, i
    for row in range(height):
        i = row * width * 3
        for col in range(width):
            canvas.SetPixel(xstart + col, ystart + row, rgb[i], rgb[i + 1], rgb[i + 2])
            i += 3


cdef class Canvas:
    cdef cppinc.Canvas* _getCanvas(self) except NULL:
        raise NotImplementedError()

    def SetImage(self, image, int offset_x=0, int offset_y=0):
        cdef cppinc.Canvas* canvas = self._getCanvas()
        cdef const uint8_t[::1] rgb
        cdef int x0, y0, x1, y1
        if image.mode != "RGB":
            raise ValueError("SetImage() needs an RGB image; convert it first with image.convert('RGB').")

        # Reads pixels through Pillow's public tobytes() rather than its internal C structs,
        # so the build needs no Pillow headers and works with any Pillow version.
        img_width, img_height = image.size
        x0 = max(0, -offset_x)
        y0 = max(0, -offset_y)
        x1 = min(img_width, canvas.width() - offset_x)
        y1 = min(img_height, canvas.height() - offset_y)
        if x1 <= x0 or y1 <= y0:
            return
        if (x0, y0, x1, y1) != (0, 0, img_width, img_height):
            image = image.crop((x0, y0, x1, y1))
        rgb = image.tobytes()
        with nogil:
            set_pixels_rgb(canvas, offset_x + x0, offset_y + y0, x1 - x0, y1 - y0, rgb)

    def Fill(self, uint8_t red, uint8_t green, uint8_t blue):
        cdef cppinc.Canvas* canvas = self._getCanvas()
        with nogil:
            canvas.Fill(red, green, blue)

    def Clear(self):
        cdef cppinc.Canvas* canvas = self._getCanvas()
        with nogil:
            canvas.Clear()

    def SetPixel(self, int x, int y, uint8_t red, uint8_t green, uint8_t blue):
        self._getCanvas().SetPixel(x, y, red, green, blue)

    @property
    def width(self):
        return self._getCanvas().width()

    @property
    def height(self):
        return self._getCanvas().height()


cdef class FrameCanvas(Canvas):
    # Frames are owned by the RGBMatrix that created them.
    cdef cppinc.Canvas* _getCanvas(self) except NULL:
        if self.__canvas == NULL:
            raise RuntimeError("Canvas was destroyed or not initialized, you cannot use this object anymore")
        return self.__canvas

    @property
    def pwmBits(self):
        return self.__canvas.pwmbits()

    @pwmBits.setter
    def pwmBits(self, uint8_t value):
        self.__canvas.SetPWMBits(value)

    @property
    def brightness(self):
        return self.__canvas.brightness()

    @brightness.setter
    def brightness(self, uint8_t value):
        self.__canvas.SetBrightness(value)


cdef class RGBMatrixOptions:
    # Cython default-constructs __options and __runtime_options with their C++ defaults.

    # RGBMatrix::Options properties
    @property
    def hardware_mapping(self):
        return self.__options.hardware_mapping.decode("utf-8")

    @hardware_mapping.setter
    def hardware_mapping(self, value):
        self.__py_encoded_hardware_mapping = value.encode("utf-8")
        self.__options.hardware_mapping = self.__py_encoded_hardware_mapping

    @property
    def rows(self):
        return self.__options.rows

    @rows.setter
    def rows(self, uint8_t value):
        self.__options.rows = value

    @property
    def cols(self):
        return self.__options.cols

    @cols.setter
    def cols(self, uint32_t value):
        self.__options.cols = value

    @property
    def chain_length(self):
        return self.__options.chain_length

    @chain_length.setter
    def chain_length(self, uint8_t value):
        self.__options.chain_length = value

    @property
    def pwm_bits(self):
        return self.__options.pwm_bits

    @pwm_bits.setter
    def pwm_bits(self, uint8_t value):
        self.__options.pwm_bits = value

    @property
    def pwm_lsb_nanoseconds(self):
        return self.__options.pwm_lsb_nanoseconds

    @pwm_lsb_nanoseconds.setter
    def pwm_lsb_nanoseconds(self, uint32_t value):
        self.__options.pwm_lsb_nanoseconds = value

    @property
    def brightness(self):
        return self.__options.brightness

    @brightness.setter
    def brightness(self, uint8_t value):
        self.__options.brightness = value

    @property
    def scan_mode(self):
        return self.__options.scan_mode

    @scan_mode.setter
    def scan_mode(self, uint8_t value):
        self.__options.scan_mode = value

    @property
    def disable_hardware_pulsing(self):
        return self.__options.disable_hardware_pulsing

    @disable_hardware_pulsing.setter
    def disable_hardware_pulsing(self, value):
        self.__options.disable_hardware_pulsing = value

    @property
    def show_refresh_rate(self):
        return self.__options.show_refresh_rate

    @show_refresh_rate.setter
    def show_refresh_rate(self, value):
        self.__options.show_refresh_rate = value

    @property
    def inverse_colors(self):
        return self.__options.inverse_colors

    @inverse_colors.setter
    def inverse_colors(self, value):
        self.__options.inverse_colors = value

    @property
    def led_rgb_sequence(self):
        return self.__options.led_rgb_sequence.decode("utf-8")

    @led_rgb_sequence.setter
    def led_rgb_sequence(self, value):
        self.__py_encoded_led_rgb_sequence = value.encode("utf-8")
        self.__options.led_rgb_sequence = self.__py_encoded_led_rgb_sequence

    @property
    def pwm_dither_bits(self):
        return self.__options.pwm_dither_bits

    @pwm_dither_bits.setter
    def pwm_dither_bits(self, uint8_t value):
        self.__options.pwm_dither_bits = value

    @property
    def limit_refresh_rate_hz(self):
        return self.__options.limit_refresh_rate_hz

    @limit_refresh_rate_hz.setter
    def limit_refresh_rate_hz(self, int value):
        self.__options.limit_refresh_rate_hz = value

    # RuntimeOptions properties
    @property
    def gpio_slowdown(self):
        return self.__runtime_options.gpio_slowdown

    @gpio_slowdown.setter
    def gpio_slowdown(self, int value):
        self.__runtime_options.gpio_slowdown = value


cdef class RGBMatrix(Canvas):
    def __cinit__(self, int rows=0, int chains=0, RGBMatrixOptions options=None):
        if options is None:
            options = RGBMatrixOptions()

        if rows > 0:
            options.rows = rows
        if chains > 0:
            options.chain_length = chains

        self.__matrix = cppinc.CreateMatrixFromOptions(options.__options, options.__runtime_options)
        if self.__matrix == NULL:
            raise RuntimeError("Could not initialize the LED matrix; the reason is printed on stderr.")

    def __dealloc__(self):
        if self.__matrix != NULL:
            # The destructor stops the refresh thread and switches the LEDs off.
            del self.__matrix
            self.__matrix = NULL

    cdef cppinc.Canvas* _getCanvas(self) except NULL:
        if self.__matrix == NULL:
            raise RuntimeError("Canvas was destroyed or not initialized, you cannot use this object anymore")
        return self.__matrix

    def CreateFrameCanvas(self):
        return __createFrameCanvas(self.__matrix.CreateFrameCanvas())

    # The optional "framerate_fraction" parameter allows to choose which
    # multiple of the global frame-count to use. So it slows down your animation
    # to an exact integer fraction of the refresh rate.
    # Default is 1, so immediately next available frame.
    # (Say you have 140Hz refresh rate, then a value of 5 would give you an
    # 28Hz animation, nicely locked to the refresh-rate).
    # If you combine this with RGBMatrixOptions.limit_refresh_rate_hz you can create
    # time-correct animations.
    def SwapOnVSync(self, FrameCanvas newFrame not None, uint8_t framerate_fraction=1):
        cdef cppinc.RGBMatrix* matrix = self.__matrix
        cdef cppinc.FrameCanvas* frame = newFrame.__canvas
        cdef cppinc.FrameCanvas* previous
        # Waiting for the next refresh can take a whole frame; let other threads run meanwhile.
        with nogil:
            previous = matrix.SwapOnVSync(frame, framerate_fraction)
        return __createFrameCanvas(previous)

    @property
    def luminanceCorrect(self):
        return self.__matrix.luminance_correct()

    @luminanceCorrect.setter
    def luminanceCorrect(self, value):
        self.__matrix.set_luminance_correct(value)

    @property
    def pwmBits(self):
        return self.__matrix.pwmbits()

    @pwmBits.setter
    def pwmBits(self, uint8_t value):
        self.__matrix.SetPWMBits(value)

    @property
    def brightness(self):
        return self.__matrix.brightness()

    @brightness.setter
    def brightness(self, uint8_t value):
        self.__matrix.SetBrightness(value)


cdef __createFrameCanvas(cppinc.FrameCanvas* newCanvas):
    canvas = FrameCanvas()
    canvas.__canvas = newCanvas
    return canvas
