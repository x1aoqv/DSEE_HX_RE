void config_default_overrides(int *sb, int *head, int *dcr, int *gain, int *offset)
{
    if (sb)
        *sb = 0;
    if (head)
        *head = 0;
    if (dcr)
        *dcr = 0;
    if (gain)
        *gain = 1;
    if (offset)
        *offset = 3;
}
