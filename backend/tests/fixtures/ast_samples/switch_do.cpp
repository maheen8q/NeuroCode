int classify(int x) {
    int r = 0;
    switch (x % 4) {
        case 0: r = 1; break;
        case 1: r = 2; break;
        default: r = 3;
    }
    do {
        if (x & 1) continue;
        r += x > 5 ? 2 : 1;
        x--;
    } while (x > 0);
    return r;
}
