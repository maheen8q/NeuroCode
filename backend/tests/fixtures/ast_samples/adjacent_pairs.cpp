int countAscents(vector<int>& a) {
    int c = 0;
    for (int i = 0; i + 1 < (int)a.size(); i++)
        if (a[i] < a[i + 1]) c++;
    for (int p = 1; p < (int)a.size(); p *= 2)
        c += a[p - 1] == a[p];
    return c;
}
