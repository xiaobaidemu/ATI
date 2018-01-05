
#define RUN_TEST(name)  extern void name(); name();

int main(int argc, char** argv)
{
    RUN_TEST(test_lock);
    RUN_TEST(test_rundown_protection);
    return 0;
}
