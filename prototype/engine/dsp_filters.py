"""Filter utilities for the Stine engine.
Reconstructed from usage in stine_unified.py.
All filters use scipy SOS for numerical stability.
"""
import numpy as np
from scipy.signal import butter, sosfilt, iirpeak, iirnotch
SR = 44100

def _hp(x, cutoff, order=2):
    """Highpass filter."""
    sos = butter(order, cutoff / (SR / 2), btype='high', output='sos')
    return sosfilt(sos, x)

def _bp(x, low, high, order=2):
    """Bandpass filter."""
    nyq = SR / 2
    sos = butter(order, [low / nyq, high / nyq], btype='band', output='sos')
    return sosfilt(sos, x)

def _peak(x, freq, Q, gain_db):
    """Parametric peak EQ (gain_db negative = cut)."""
    b, a = iirpeak(freq / (SR / 2), Q)
    # iirpeak gives unity-gain notch; mix in proportionally for gain_db
    # Use manual biquad: b,a from scipy iirpeak are for a notch at +/-inf
    # Better: build a proper peaking EQ biquad
    w0 = 2 * np.pi * freq / SR
    A = 10 ** (gain_db / 40.0)
    alpha = np.sin(w0) / (2 * Q)
    b0 = 1 + alpha * A
    b1 = -2 * np.cos(w0)
    b2 = 1 - alpha * A
    a0 = 1 + alpha / A
    a1 = -2 * np.cos(w0)
    a2 = 1 - alpha / A
    b_c = np.array([b0/a0, b1/a0, b2/a0])
    a_c = np.array([1.0,   a1/a0, a2/a0])
    from scipy.signal import lfilter
    return lfilter(b_c, a_c, x)

def _hshelf(x, freq, gain_db):
    """High shelf EQ."""
    w0 = 2 * np.pi * freq / SR
    A = 10 ** (gain_db / 40.0)
    S = 1.0  # shelf slope
    alpha = np.sin(w0) / 2 * np.sqrt((A + 1/A) * (1/S - 1) + 2)
    cos_w0 = np.cos(w0)
    b0 =      A * ((A+1) + (A-1)*cos_w0 + 2*np.sqrt(A)*alpha)
    b1 = -2 * A * ((A-1) + (A+1)*cos_w0)
    b2 =      A * ((A+1) + (A-1)*cos_w0 - 2*np.sqrt(A)*alpha)
    a0 =           (A+1) - (A-1)*cos_w0 + 2*np.sqrt(A)*alpha
    a1 =  2 *     ((A-1) - (A+1)*cos_w0)
    a2 =           (A+1) - (A-1)*cos_w0 - 2*np.sqrt(A)*alpha
    b_c = np.array([b0/a0, b1/a0, b2/a0])
    a_c = np.array([1.0,   a1/a0, a2/a0])
    from scipy.signal import lfilter
    return lfilter(b_c, a_c, x)
