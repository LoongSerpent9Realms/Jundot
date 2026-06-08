using System.Collections.Immutable;
using Microsoft.CodeAnalysis;

namespace Jundot.SourceGenerators
{
    public readonly struct JundotMethodData
    {
        public JundotMethodData(IMethodSymbol method, ImmutableArray<MarshalType> paramTypes,
            ImmutableArray<ITypeSymbol> paramTypeSymbols, (MarshalType MarshalType, ITypeSymbol TypeSymbol)? retType)
        {
            Method = method;
            ParamTypes = paramTypes;
            ParamTypeSymbols = paramTypeSymbols;
            RetType = retType;
        }

        public IMethodSymbol Method { get; }
        public ImmutableArray<MarshalType> ParamTypes { get; }
        public ImmutableArray<ITypeSymbol> ParamTypeSymbols { get; }
        public (MarshalType MarshalType, ITypeSymbol TypeSymbol)? RetType { get; }
    }

    public readonly struct JundotSignalDelegateData
    {
        public JundotSignalDelegateData(string name, INamedTypeSymbol delegateSymbol, JundotMethodData invokeMethodData)
        {
            Name = name;
            DelegateSymbol = delegateSymbol;
            InvokeMethodData = invokeMethodData;
        }

        public string Name { get; }
        public INamedTypeSymbol DelegateSymbol { get; }
        public JundotMethodData InvokeMethodData { get; }
    }

    public readonly struct JundotPropertyData
    {
        public JundotPropertyData(IPropertySymbol propertySymbol, MarshalType type)
        {
            PropertySymbol = propertySymbol;
            Type = type;
        }

        public IPropertySymbol PropertySymbol { get; }
        public MarshalType Type { get; }
    }

    public readonly struct JundotFieldData
    {
        public JundotFieldData(IFieldSymbol fieldSymbol, MarshalType type)
        {
            FieldSymbol = fieldSymbol;
            Type = type;
        }

        public IFieldSymbol FieldSymbol { get; }
        public MarshalType Type { get; }
    }

    public struct JundotPropertyOrFieldData
    {
        public JundotPropertyOrFieldData(ISymbol symbol, MarshalType type)
        {
            Symbol = symbol;
            Type = type;
        }

        public JundotPropertyOrFieldData(JundotPropertyData propertyData)
            : this(propertyData.PropertySymbol, propertyData.Type)
        {
        }

        public JundotPropertyOrFieldData(JundotFieldData fieldData)
            : this(fieldData.FieldSymbol, fieldData.Type)
        {
        }

        public ISymbol Symbol { get; }
        public MarshalType Type { get; }
    }
}
