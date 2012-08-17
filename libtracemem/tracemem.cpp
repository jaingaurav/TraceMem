#include <iostream>
#include <fstream>
#include <ctime>

using namespace std;

struct Data {
    union {
        int32_t d_int32;
    };
};

class Trace {
    public:
    enum Type {
        INT32
    };

    Trace() {}
    Trace(Type t, int32_t s, void * a) : type(t), seq(s), addr(a), read(true) {
        switch (type) {
            case INT32:
                data.d_int32 = *(int32_t*)addr;
                break;
        }
    }

    Trace(Type t, int32_t s, void * a, Data d) : type(t), seq(s), addr(a), data(d), read(false) {
    }

    void dump(ofstream& myfile) {
        if (read)
                myfile << "R:" << seq << ":" << addr << ":" << data.d_int32 << "\n";
        else
                myfile << "W:" << seq << ":" << addr << ":" << data.d_int32 << "\n";
    }

    private:
    Type type;
    int32_t seq;
    void* addr;
    Data data;
    bool read;
};

class Tracer {
    public:
        Tracer() : num_traces(0), index(0), size(0) {
            char *num = getenv("TRACE_MEM_TRACES");
            if (num) {
                num_traces = atoi(num);
            }
            if (!num_traces) {
                num_traces = 64;
            }

            traces = new Trace[num_traces];
            myfile.open ("trace.txt");
        }
        ~Tracer() {
            unsigned printed = 0;
            while (size != printed) {
                int cur_index = (index-(size-printed))%num_traces;
                if (cur_index == 0)
                    myfile << "Time: " << time << "\n";
                traces[cur_index].dump(myfile);
                ++printed;
            }
            myfile.close();
        }
        int32_t readi32(int32_t seq, int32_t* addr) {
            if (index == 0)
                time = std::time(0);
            traces[index] = Trace(Trace::INT32, seq, addr);
            ++index;
            index %= num_traces;
            if (size < num_traces) {
                ++size;
            }
            return *addr;
        }

        void writei32(int32_t seq, int32_t* addr, int32_t data) {
            Data d;
            d.d_int32 = data;
            if (index == 0)
                time = std::time(0);
            traces[index] = Trace(Trace::INT32, seq, addr, d);
            ++index;
            index %= 64;
            if (size < 64) {
                ++size;
            }
            *addr = data;
        }

    private:
        Trace * traces;
        int num_traces;
        int index;
        int size;
        std::time_t time;

        ofstream myfile;
};

Tracer tracer;

extern "C" int32_t trace_readi32(int32_t seq, int32_t* addr) {
    return tracer.readi32(seq, addr);
}

extern "C" void trace_writei32(int32_t seq, int32_t* addr, int32_t data) {
    tracer.writei32(seq, addr, data);
}
