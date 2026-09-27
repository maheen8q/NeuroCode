#include <vector>
using namespace std;

bool solve(vector<int> &a, int k)
{
    for (int i = 0; i < (int)a.size(); i++)
        for (int j = i + 1; j < (int)a.size(); j++)
            if (a[i] + a[j] == k)
                return true;
    return false;
}