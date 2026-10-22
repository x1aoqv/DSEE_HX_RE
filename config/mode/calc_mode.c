#define EXPORT __declspec(dllexport)

EXPORT int dsee_mode(int enable, int device_rate, int codec)
{
    if (!enable)
        return 0;
    if (device_rate <= 48000)
        return 1;
    if (codec == 16 || codec == 101)
        return 2;
    return 3;
}
