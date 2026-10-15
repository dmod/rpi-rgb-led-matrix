from . cimport cppinc

cdef class Canvas:
    cdef cppinc.Canvas *_getCanvas(self) except NULL

cdef class FrameCanvas(Canvas):
    cdef cppinc.FrameCanvas *__canvas

cdef class RGBMatrix(Canvas):
    cdef cppinc.RGBMatrix *__matrix

cdef class RGBMatrixOptions:
    cdef cppinc.Options __options
    cdef cppinc.RuntimeOptions __runtime_options
    # The Options struct only stores pointers to these strings, so keep the
    # encoded bytes alive for as long as the options are.
    cdef bytes __py_encoded_hardware_mapping
    cdef bytes __py_encoded_led_rgb_sequence
