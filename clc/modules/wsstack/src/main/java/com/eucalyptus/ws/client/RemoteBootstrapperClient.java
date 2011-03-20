/*******************************************************************************
 *Copyright (c) 2009 Eucalyptus Systems, Inc.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, only version 3 of the License.
 * 
 * 
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 * Please contact Eucalyptus Systems, Inc., 130 Castilian
 * Dr., Goleta, CA 93101 USA or visit <http://www.eucalyptus.com/licenses/>
 * if you need additional information or have any questions.
 * 
 * This file may incorporate work covered under the following copyright and
 * permission notice:
 * 
 * Software License Agreement (BSD License)
 * 
 * Copyright (c) 2008, Regents of the University of California
 * All rights reserved.
 * 
 * Redistribution and use of this software in source and binary forms, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. USERS OF
 * THIS SOFTWARE ACKNOWLEDGE THE POSSIBLE PRESENCE OF OTHER OPEN SOURCE
 * LICENSED MATERIAL, COPYRIGHTED MATERIAL OR PATENTED MATERIAL IN THIS
 * SOFTWARE, AND IF ANY SUCH MATERIAL IS DISCOVERED THE PARTY DISCOVERING
 * IT MAY INFORM DR. RICH WOLSKI AT THE UNIVERSITY OF CALIFORNIA, SANTA
 * BARBARA WHO WILL THEN ASCERTAIN THE MOST APPROPRIATE REMEDY, WHICH IN
 * THE REGENTS' DISCRETION MAY INCLUDE, WITHOUT LIMITATION, REPLACEMENT
 * OF THE CODE SO IDENTIFIED, LICENSING OF THE CODE SO IDENTIFIED, OR
 * WITHDRAWAL OF THE CODE CAPABILITY TO THE EXTENT NEEDED TO COMPLY WITH
 * ANY SUCH LICENSES OR RIGHTS.
 *******************************************************************************/
/*
 * @author chris grzegorczyk <grze@eucalyptus.com>
 */
package com.eucalyptus.ws.client;

import java.security.GeneralSecurityException;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.Executors;
import org.apache.log4j.Logger;
import org.jboss.netty.channel.ChannelDownstreamHandler;
import org.jboss.netty.channel.ChannelEvent;
import org.jboss.netty.channel.ChannelFactory;
import org.jboss.netty.channel.ChannelHandlerContext;
import org.jboss.netty.channel.ChannelPipeline;
import org.jboss.netty.channel.ChannelPipelineCoverage;
import org.jboss.netty.channel.ChannelPipelineFactory;
import org.jboss.netty.channel.ChannelUpstreamHandler;
import org.jboss.netty.channel.Channels;
import org.jboss.netty.channel.ExceptionEvent;
import org.jboss.netty.channel.socket.nio.NioClientSocketChannelFactory;
import org.jboss.netty.handler.codec.http.HttpRequestEncoder;
import org.jboss.netty.handler.codec.http.HttpResponseDecoder;
import org.jboss.netty.handler.stream.ChunkedWriteHandler;
import com.eucalyptus.binding.BindingManager;
import com.eucalyptus.bootstrap.Bootstrap;
import com.eucalyptus.bootstrap.Bootstrapper;
import com.eucalyptus.bootstrap.DependsLocal;
import com.eucalyptus.bootstrap.Provides;
import com.eucalyptus.bootstrap.RunDuring;
import com.eucalyptus.bootstrap.Bootstrap.Stage;
import com.eucalyptus.component.Component;
import com.eucalyptus.component.Components;
import com.eucalyptus.component.Service;
import com.eucalyptus.component.ServiceConfiguration;
import com.eucalyptus.component.event.DisableComponentEvent;
import com.eucalyptus.component.event.EnableComponentEvent;
import com.eucalyptus.component.event.LifecycleEvent;
import com.eucalyptus.component.event.StartComponentEvent;
import com.eucalyptus.component.event.StopComponentEvent;
import com.eucalyptus.component.id.Eucalyptus;
import com.eucalyptus.component.id.Storage;
import com.eucalyptus.component.id.Walrus;
import com.eucalyptus.config.ComponentConfiguration;
import com.eucalyptus.config.Configuration;
import com.eucalyptus.empyrean.Empyrean;
import com.eucalyptus.event.ClockTick;
import com.eucalyptus.event.Event;
import com.eucalyptus.event.EventListener;
import com.eucalyptus.util.LogUtil;
import com.eucalyptus.util.Internets;
import com.eucalyptus.ws.handlers.BindingHandler;
import com.eucalyptus.ws.handlers.InternalWsSecHandler;
import com.eucalyptus.ws.handlers.SoapMarshallingHandler;
import com.eucalyptus.ws.protocol.AddressingHandler;
import com.eucalyptus.ws.protocol.SoapHandler;
import com.eucalyptus.ws.util.NioBootstrap;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Multimap;
import com.google.common.collect.Multimaps;

@Provides(Empyrean.class)
@RunDuring(Bootstrap.Stage.RemoteConfiguration)
@DependsLocal(Eucalyptus.class)
public class RemoteBootstrapperClient extends Bootstrapper implements ChannelPipelineFactory, EventListener {
  private static Logger                            LOG    = Logger.getLogger( RemoteBootstrapperClient.class );
  private ConcurrentMap<String, HeartbeatClient>   heartbeatMap;
  private NioBootstrap                             clientBootstrap;
  private ChannelFactory                           channelFactory;
  private static RemoteBootstrapperClient          client = new RemoteBootstrapperClient( );

  public static RemoteBootstrapperClient getInstance( ) {
    return client;
  }

  private RemoteBootstrapperClient( ) {
    this.channelFactory = new NioClientSocketChannelFactory( Executors.newCachedThreadPool( ), Executors.newCachedThreadPool( ) );
    this.clientBootstrap = new NioBootstrap( channelFactory );
    this.clientBootstrap.setPipelineFactory( this );
  }

  public ChannelPipeline getPipeline( ) throws Exception {
    ChannelPipeline pipeline = Channels.pipeline( );
    pipeline.addLast( "decoder", new HttpRequestEncoder( ) );
    pipeline.addLast( "encoder", new HttpResponseDecoder( ) );
    pipeline.addLast( "chunkedWriter", new ChunkedWriteHandler( ) );
    pipeline.addLast( "deserialize", new SoapMarshallingHandler( ) );
    try {
      pipeline.addLast( "ws-security", new InternalWsSecHandler( ) );
    } catch ( GeneralSecurityException e ) {
      LOG.error( e, e );
    }
    pipeline.addLast( "ws-addressing", new AddressingHandler( ) );
    pipeline.addLast( "build-soap-envelope", new SoapHandler( ) );
    pipeline.addLast( "binding", new BindingHandler( BindingManager.getBinding( "msgs_eucalyptus_com" ) ) );
    pipeline.addLast( "handler", new HeartbeatHandler( ) );
    return pipeline;
  }

  @ChannelPipelineCoverage( "one" )
  class HeartbeatHandler implements ChannelDownstreamHandler, ChannelUpstreamHandler {

    @Override
    public void handleDownstream( ChannelHandlerContext ctx, ChannelEvent e ) {
      if ( e instanceof ExceptionEvent ) {
        LOG.error( ( ( ExceptionEvent ) e ).getCause( ), ( ( ExceptionEvent ) e ).getCause( ) );
        ctx.getChannel( ).close( );
      } else {
        ctx.sendDownstream( e );
      }
    }

    @Override
    public void handleUpstream( ChannelHandlerContext ctx, ChannelEvent e ) {
      if ( e instanceof ExceptionEvent ) {
        LOG.error( ( ( ExceptionEvent ) e ).getCause( ), ( ( ExceptionEvent ) e ).getCause( ) );
        ctx.getChannel( ).close( );
      } else {
        ctx.sendUpstream( e );
      }
    }

  }

  @Override
  public boolean load( ) throws Exception {
    return true;
  }

  @Override
  public boolean start( ) throws Exception {
    return true;
  }

  /**
   * @see com.eucalyptus.bootstrap.Bootstrapper#enable()
   */
  @Override
  public boolean enable( ) throws Exception {
    return true;
  }

  /**
   * @see com.eucalyptus.bootstrap.Bootstrapper#stop()
   */
  @Override
  public boolean stop( ) throws Exception {
    return true;
  }

  /**
   * @see com.eucalyptus.bootstrap.Bootstrapper#destroy()
   */
  @Override
  public void destroy( ) throws Exception {}

  /**
   * @see com.eucalyptus.bootstrap.Bootstrapper#disable()
   */
  @Override
  public boolean disable( ) throws Exception {
    return true;
  }

  /**
   * @see com.eucalyptus.bootstrap.Bootstrapper#check()
   */
  @Override
  public boolean check( ) throws Exception {
    return true;
  }

  @Override
  public void fireEvent( Event event ) {
    if ( event instanceof LifecycleEvent ) {
      LifecycleEvent e = ( LifecycleEvent ) event;
      if ( !Walrus.class.equals( e.getIdentity( ).getClass( ) ) && !Storage.class.equals( e.getIdentity( ).getClass( ) ) ) {
        return;
      } else if ( e.getConfiguration( ).getPort( ) < 0 ) {
        return;
      } else {
        ServiceConfiguration config = e.getConfiguration( );
        if ( event instanceof StartComponentEvent ) {
          this.fireHeartbeat( );
        } else if ( event instanceof StopComponentEvent ) {
          this.fireHeartbeat( );
        } else if ( event instanceof EnableComponentEvent ) {
          this.fireHeartbeat( );
        } else if ( event instanceof DisableComponentEvent ) {
          this.fireHeartbeat( );
        }
      }
    } else if ( event instanceof ClockTick ) {
      if ( ( ( ClockTick ) event ).isBackEdge( ) ) {
        this.fireHeartbeat( );
      }
    }
  }

  private void fireHeartbeat( ) {
    Multimap<String,ServiceConfiguration> services = ArrayListMultimap.create( );
    for( Component c : Components.list( ) ) {
      if( !c.getComponentId( ).isCloudLocal( ) && !c.getComponentId( ).isAlwaysLocal( ) ) {
        for( Service s : c.lookupServices( ) ) {
          if( s.isLocal( ) ) {
            services.put( s.getHost( ), s.getServiceConfiguration( ) );
          }
        }
      }
    }
    Set<String> currentHosts = services.keySet( );
    for( String host : this.heartbeatMap.keySet( ) ) {
      if( !currentHosts.contains( host ) ) {
        HeartbeatClient staleHb = this.heartbeatMap.remove( host );
        LOG.debug( "Removing stale heartbeat client for " + host );
        staleHb.close( );
      }
    }
    for( String host : services.keySet( ) ) {
      if( !heartbeatMap.containsKey( host ) ) {
        heartbeatMap.put( host, new HeartbeatClient( this.clientBootstrap, host, 8773 /** ASAP:GRZE:FIXME **/ ) );
      }
      HeartbeatClient hb = this.heartbeatMap.get( host );
      Collection<ServiceConfiguration> currentServices = services.get( host );
      LOG.debug( "Sending heartbeat to: " + hb.getHostName( ) + " with " + services ); 
      hb.send( currentServices );
    }
  }

}
