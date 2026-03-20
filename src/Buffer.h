#pragma once

enum BufferDataType
{
    Byte,
    Word,
    Int,
    UInt,
    Float
};

inline int GetBufferDataSize(BufferDataType t)
{
    switch(t)
    {
        case Byte:return 1;
        case Word:return 2;
        case Int:case UInt:return 4;
        case Float:return 4;
    }
};

template<class T>
class Buffer
{
public:
    T* Data = nullptr;

    BufferDataType DataType;



};