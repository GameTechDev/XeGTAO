///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"

#include "vaRendering.h"

#include "Rendering/Shaders/vaSharedTypes_PrimitiveShapeRenderer.h"

//#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaRenderBuffers.h"

#include "Rendering/vaShader.h"

namespace Vanilla
{
    struct vaRenderOutputs;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // !!!! UNFINISHED, WORK IN PROGRESS !!!!
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Used to batch and render various simple shapes with one render call.
    //
    // 3D shape coordinates are expected in world space (drawn using vaDrawAttributes.Camera) and 2D shape coordinates
    // are expected in NDC space.
    //
    // The order in which the shapes are added is the order in which they get rendered.
    //
    // Support for texture mapping, custom shaders, etc. will be added in the future.
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class vaPrimitiveShapeRenderer : public vaRenderingModule
    {
    public:
        struct DrawSettings
        {
            bool        UseDepth;
            bool        WriteDepth;
            bool        AlphaBlend;
            vaFaceCull  CullMode;
            bool        Wireframe;
            vaVector4   ColorMultiplier;

            DrawSettings( bool useDepth = true, bool writeDepth = true, bool alphaBlend = false, vaFaceCull cullMode = vaFaceCull::Back, bool wireframe = false, const vaVector4 & colorMultiplier = vaVector4( 1, 1, 1, 1 ) )
                : UseDepth        (useDepth       ),
                WriteDepth      (writeDepth     ),
                AlphaBlend      (alphaBlend     ),
                CullMode        (cullMode       ),
                Wireframe       (wireframe      ),
                ColorMultiplier (colorMultiplier)
            { }
        };

    protected:
        static const int                        c_totalVertexCount      = 4 * 1024 * 1024;
        static const int                        c_totalShapeBufferSize  = 4 * 1024 * 1024;

        vaTypedVertexBufferWrapper< uint64 >    m_vertexBufferGPU;
        int                                     m_verticesToDraw;
        vaTypedStructuredBufferWrapper< uint32 >    m_shapeInfoBufferGPU;

        std::vector< uint64 >                   m_vertexBuffer;
        std::vector< uint32 >                   m_shapeInfoBuffer;
        bool                                    m_buffersDirty;


        vaTypedConstantBufferWrapper<PrimitiveShapeRendererShaderConstants, true>  
                                                m_constantsBuffer;

        vaAutoRMI<vaVertexShader>           m_vertexShader;
        vaAutoRMI<vaPixelShader>            m_pixelShader;

    protected:
        vaPrimitiveShapeRenderer( const vaRenderingModuleParams & params );
    public:
        virtual ~vaPrimitiveShapeRenderer( )    { }

        void                                    AddTriangle( const vaVector3 & a, const vaVector3 & b, const vaVector3 & c, const vaVector4 & color );
        void                                    AddCylinder( float height, float radiusBottom, float radiusTop, int tessellation, bool openTopBottom, const vaVector4 & color, const vaMatrix4x4 & transform );

    protected:
        virtual void                            UpdateConstants( vaRenderDeviceContext & renderContext, const DrawSettings& drawSettings );

    public:
        // never call more than once per frame or you will cause a lock - if that is needed, upgrade the buffer swapping/mapping
        virtual void                            Draw( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaDrawAttributes & drawAttributes, DrawSettings & drawSettings = DrawSettings(), bool clearCollected = true );
    };
}