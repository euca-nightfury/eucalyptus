/*************************************************************************
 * Copyright 2009-2013 Eucalyptus Systems, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 *
 * Please contact Eucalyptus Systems, Inc., 6755 Hollister Ave., Goleta
 * CA 93117, USA or visit http://www.eucalyptus.com/licenses/ if you need
 * additional information or have any questions.
 ************************************************************************/

package com.eucalyptus.loadbalancing;

import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.NoSuchElementException;
import java.util.Set;

import javax.annotation.Nullable;
import javax.persistence.EntityTransaction;

import org.apache.log4j.Logger;

import com.eucalyptus.auth.Accounts;
import com.eucalyptus.auth.AuthQuotaException;
import com.eucalyptus.auth.principal.User;
import com.eucalyptus.auth.principal.UserFullName;
import com.eucalyptus.cloudwatch.MetricData;
import com.eucalyptus.context.Context;
import com.eucalyptus.context.Contexts;
import com.eucalyptus.entities.Entities;
import com.eucalyptus.event.EventFailedException;
import com.eucalyptus.loadbalancing.LoadBalancerListener.PROTOCOL;
import com.eucalyptus.loadbalancing.activities.CreateListenerEvent;
import com.eucalyptus.loadbalancing.activities.DeleteListenerEvent;
import com.eucalyptus.loadbalancing.activities.DeleteLoadbalancerEvent;
import com.eucalyptus.loadbalancing.activities.DeregisterInstancesEvent;
import com.eucalyptus.loadbalancing.activities.DisabledZoneEvent;
import com.eucalyptus.loadbalancing.activities.EnabledZoneEvent;
import com.eucalyptus.loadbalancing.activities.LoadBalancerServoInstance;
import com.eucalyptus.loadbalancing.activities.NewLoadbalancerEvent;
import com.eucalyptus.loadbalancing.activities.ActivityManager;
import com.eucalyptus.loadbalancing.activities.RegisterInstancesEvent;
import com.eucalyptus.util.EucalyptusCloudException;
import com.eucalyptus.util.Exceptions;
import com.google.common.base.Function;
import com.google.common.base.Supplier;
import com.google.common.base.Predicate;
import com.google.common.collect.Collections2;
import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import com.google.common.collect.Sets;
import com.google.common.net.HostSpecifier;

/**
 * @author Sang-Min Park
 */
public class LoadBalancingService {
  private static Logger    LOG     = Logger.getLogger( LoadBalancingService.class );
  
  private boolean isValidServoRequest(String instanceId){
      final Context ctx = Contexts.lookup( );
	  try{
		  final LoadBalancerServoInstance instance = LoadBalancers.lookupServoInstance(instanceId);
		  InetSocketAddress remoteAddr = ( ( InetSocketAddress ) ctx.getChannel( ).getRemoteAddress( ) );
		  String remoteHost = remoteAddr.getAddress( ).getHostAddress( );
		  if(! (remoteHost.equals(instance.getAddress()) || remoteHost.equals(instance.getPrivateIp())))
				  return false;
		  return true;
	  }catch(Exception ex){
		  return false;
	  }
  }
  
  /// EUCA-specific, internal operations for fetching listener specification
  public DescribeLoadBalancersByServoResponseType describeLoadBalancersByServo(DescribeLoadBalancersByServoType request) throws EucalyptusCloudException {
  	  final DescribeLoadBalancersByServoResponseType reply = request.getReply();
  	  final String instanceId = request.getInstanceId();
	  // lookup servo instance Id to see which LB zone it is assigned to
	  LoadBalancerZone zone = null;
	  try{
	  	  if(isValidServoRequest(instanceId)){
	  		  final LoadBalancerServoInstance instance = LoadBalancers.lookupServoInstance(instanceId);
	  		  zone = instance.getAvailabilityZone();
	  	  }
	  }catch(NoSuchElementException ex){
  		;
  	  }catch(Exception ex){
  		LOG.warn("failed to query loadbalancer for servo instance: "+instanceId);
  	  }
	  
	  
  	  final Function<LoadBalancerZone, Set<LoadBalancerDescription>> lookupLBDescriptions = new Function<LoadBalancerZone, Set<LoadBalancerDescription>> () {
		  @Override
  		  public Set<LoadBalancerDescription> apply (LoadBalancerZone zone){
	    		final Set<LoadBalancerDescription> descs = Sets.newHashSet();
	    		final LoadBalancer lb = zone.getLoadbalancer();
	    		final String lbName = lb.getDisplayName();

	    		LoadBalancerDescription desc = new LoadBalancerDescription();
	    		desc.setLoadBalancerName(lbName); /// loadbalancer name
	    		desc.setCreatedTime(lb.getCreationTimestamp());/// createdtime
	    		final LoadBalancerDnsRecord dns = lb.getDns();
	    			
	    		desc.setDnsName(dns.getDnsName());           /// dns name
	    		
	    		Collection<LoadBalancerBackendInstance> notInError =
	    				Collections2.filter(zone.getBackendInstances(), new Predicate<LoadBalancerBackendInstance>(){
							@Override
							public boolean apply(
									@Nullable LoadBalancerBackendInstance arg0) {
								return ! LoadBalancerBackendInstance.STATE.Error.equals(arg0.getBackendState());
							}
	    				});
	    		
	    		if(notInError.size()>0){
  		  			desc.setInstances(new Instances());
	    			desc.getInstances().setMember(new ArrayList<Instance>(
	    		    	Collections2.transform(notInError, new Function<LoadBalancerBackendInstance, Instance>(){
    		    			@Override
    		    			public Instance apply(final LoadBalancerBackendInstance be){
    		    				Instance instance = new Instance();
    		    				// re-use instanceId field to mark the instance's IP 
    		    				// servo tool should interpret it properly
    		    				instance.setInstanceId(be.getInstanceId() +":"+ be.getIpAddress());
    		    				return instance;
    		    			}
    		    		})));
	    		}
	    			/// availability zones
	    		desc.setAvailabilityZones(new AvailabilityZones());
	    		desc.getAvailabilityZones().setMember(Lists.newArrayList(zone.getName()));
	    			                                  /// listeners
	    		if(lb.getListeners().size()>0){
	    			desc.setListenerDescriptions(new ListenerDescriptions());
	    			desc.getListenerDescriptions().setMember(new ArrayList<ListenerDescription>(
	    					Collections2.transform(lb.getListeners(), new Function<LoadBalancerListener, ListenerDescription>(){
    							@Override
    							public ListenerDescription apply(final LoadBalancerListener input){
    								ListenerDescription desc = new ListenerDescription();
    								Listener listener = new Listener();
    								listener.setLoadBalancerPort(input.getLoadbalancerPort());
    								listener.setInstancePort(input.getInstancePort());
    								if(input.getInstanceProtocol() != PROTOCOL.NONE)
    									listener.setInstanceProtocol(input.getInstanceProtocol().name());
    								listener.setProtocol(input.getProtocol().name());
    								if(input.getCertificateId()!=null)
    									listener.setSslCertificateId(input.getCertificateId());
    								desc.setListener(listener);
    								return desc;
    							}
    						})));
    			}
	    			                                  /// health check
	    		try{
	 				  int interval = lb.getHealthCheckInterval();
	 				  String target = lb.getHealthCheckTarget();
	 				  int timeout = lb.getHealthCheckTimeout();
	 				  int healthyThresholds = lb.getHealthyThreshold();
	 				  int unhealthyThresholds = lb.getHealthCheckUnhealthyThreshold();
	 				  
	 				  final HealthCheck hc = new HealthCheck();
	 				  hc.setInterval(interval);
	 				  hc.setHealthyThreshold(healthyThresholds);
	 				  hc.setTarget(target);
	 				  hc.setTimeout(timeout);
	 				  hc.setUnhealthyThreshold(unhealthyThresholds);
	 				  desc.setHealthCheck(hc);
	    		}catch(IllegalStateException ex){
	    				;
	    		}catch(Exception ex){
	    				;
	    		}
    			descs.add(desc);
	    		return descs;
	    	}
  	  };

	  Set<LoadBalancerDescription> descs = null;
  	  if(zone != null && LoadBalancingMetadatas.filterPrivilegedWithoutOwner().apply( zone.getLoadbalancer() ) && zone.getState().equals(LoadBalancerZone.STATE.InService)){
  			 descs= lookupLBDescriptions.apply(zone);
  	  }else
  		  descs = Sets.<LoadBalancerDescription>newHashSet();
  		  
	  DescribeLoadBalancersResult descResult = new DescribeLoadBalancersResult();
	  LoadBalancerDescriptions lbDescs = new LoadBalancerDescriptions();
	  lbDescs.setMember(new ArrayList<LoadBalancerDescription>(descs));
	  descResult.setLoadBalancerDescriptions(lbDescs);
	  reply.setDescribeLoadBalancersResult(descResult);
	  reply.set_return(true);
	    
	  return reply;
  }
  
  /// EUCA-specific, internal operations for storing instance health check and cloudwatch metrics
  public PutServoStatesResponseType putServoStates(PutServoStatesType request){
	  PutServoStatesResponseType reply = request.getReply();
	  final String servoId = request.getInstanceId();

	  if(! isValidServoRequest(servoId)){
		  return reply;
	  }
	  
	  final Instances instances = request.getInstances();
	  final MetricData metric = request.getMetricData();
	  
	  LoadBalancer lb = null;
	  if(servoId!= null){
		  try{
			  LoadBalancerServoInstance servo = LoadBalancers.lookupServoInstance(servoId);
			  lb = servo.getAvailabilityZone().getLoadbalancer();
			  if(lb==null)
				  throw new Exception("Failed to find the loadbalancer");
			  if(! servo.getAvailabilityZone().getState().equals(LoadBalancerZone.STATE.InService))
				  lb= null;
		  }catch(NoSuchElementException ex){
			  LOG.warn("unknown servo VM id is used to query: "+servoId);
		  }catch(Exception ex){
			  LOG.warn("failed to query servo instance");
		  }
	  }
	  if(lb==null || !LoadBalancingMetadatas.filterPrivilegedWithoutOwner().apply( lb ))
		  return reply;
	  
	  /// INSTANCE HEALTH CHECK UPDATE
	  if(instances!= null && instances.getMember()!=null && instances.getMember().size()>0){
		  final Collection<LoadBalancerBackendInstance> lbInstances = lb.getBackendInstances();
		  if(lb != null && instances.getMember()!=null && instances.getMember().size()>0){
			  for(Instance instance : instances.getMember()){
				  String instanceId = instance.getInstanceId();
				  // format: instanceId:state
				  String[] parts = instanceId.split(":");
				  if(parts==null || parts.length!= 2){
					  LOG.warn("instance id is in wrong format:"+ instanceId);
					  continue;
				  }
				  instanceId = parts[0];
				  String state = parts[1];
				  
				  LoadBalancerBackendInstance found = null;
				  for(final LoadBalancerBackendInstance lbInstance : lbInstances){
					  if(instanceId.equals(lbInstance.getInstanceId())){
						  found = lbInstance;
						  break;
					  }  
				  }
				  if(found!=null){
					  String zoneName = found.getAvailabilityZone().getName();
					  final EntityTransaction db = Entities.get( LoadBalancerBackendInstance.class );
				 	  try{
				 		  found = Entities.uniqueResult(found);
				 		  if (state.equals(LoadBalancerBackendInstance.STATE.InService.name()) || 
				 				  state.equals(LoadBalancerBackendInstance.STATE.OutOfService.name())){
				 			  found.setState(Enum.valueOf(LoadBalancerBackendInstance.STATE.class, state));
				 			 
				 			  if(state.equals(LoadBalancerBackendInstance.STATE.OutOfService.name())){
				 				  found.setReasonCode("Instance");
				 				  found.setDescription("Instance has failed at least the UnhealthyThreshold number of health checks consecutively.");
				 			  } else{
				 				  found.setReasonCode(null);
				 				  found.setDescription(null);
				 			  }  
				 			  Entities.persist(found);
				 		  }
				 		  db.commit();
				 	  }catch(NoSuchElementException ex){
				 		  db.rollback();
				 	  }catch(Exception ex){
				 		  db.rollback();
				 		  LOG.warn("Failed to query loadbalancer backend instance: "+instanceId, ex);
				 	  }
				  }
			  }
		  }
	  }
	  
	  /// Update Cloudwatch
	  for(final LoadBalancerBackendInstance sample : lb.getBackendInstances()){
		  final EntityTransaction db = Entities.get( LoadBalancerBackendInstance.class );
	 	  try{
	 		  final LoadBalancerBackendInstance found = Entities.uniqueResult(sample);
	 		  final String zoneName = found.getAvailabilityZone().getName();
	 		  if(found.getState().equals(LoadBalancerBackendInstance.STATE.InService)){
	 			  LoadBalancerCwatchMetrics.getInstance().updateHealthy(lb.getOwnerUserId(), lb.getDisplayName(), zoneName, found.getInstanceId());
	 		  }else if (found.getState().equals(LoadBalancerBackendInstance.STATE.OutOfService)){
	 			  LoadBalancerCwatchMetrics.getInstance().updateUnHealthy(lb.getOwnerUserId(), lb.getDisplayName(), zoneName, found.getInstanceId());
	 		  }
	 		  db.commit();
	 	  }catch(NoSuchElementException ex){
	 		  db.rollback();
	 	  }catch(Exception ex){
	 		  db.rollback();
	 		  LOG.warn("Failed to query loadbalancer backend instance", ex);
	 	  }
	  }
	  
	  if(metric!= null && metric.getMember()!= null && metric.getMember().size()>0){
		  try{
			  LoadBalancerCwatchMetrics.getInstance().addMetric(servoId, metric);
		  }catch(Exception ex){
			  LOG.error("Failed to add ELB cloudwatch metric", ex);
		  }
	  }
	  
	  return reply;
  }
 
  public CreateLoadBalancerResponseType createLoadBalancer(CreateLoadBalancerType request) throws EucalyptusCloudException {
    final CreateLoadBalancerResponseType reply = request.getReply();
    final Context ctx = Contexts.lookup();
    final UserFullName ownerFullName = ctx.getUserFullName();
    final String lbName = request.getLoadBalancerName();

    // verify loadbalancer name
    final Predicate<String> nameChecker = new Predicate<String>(){
		@Override
		public boolean apply(@Nullable String arg0) {
			if(arg0==null)
				return false;
			if(!HostSpecifier.isValid(String.format("%s.com", arg0)))
				return false;
			return true;
		}
    };
    
    if(!nameChecker.apply(lbName)){
    	throw new LoadBalancingException("Invalid character found in the loadbalancer name");
    }
    if(request.getListeners()!=null && request.getListeners().getMember()!=null)
    	LoadBalancers.validateListener(request.getListeners().getMember());
    
    final Supplier<LoadBalancer> allocator = new Supplier<LoadBalancer>() {
      @Override
      public LoadBalancer get() {
        try {
          return LoadBalancers.addLoadbalancer(ownerFullName, lbName);
        } catch ( LoadBalancingException e ) {
          throw Exceptions.toUndeclared( e );
        }
      }
    };

    LoadBalancer lb = null;
    try {
      lb = LoadBalancingMetadatas.allocateUnitlessResource( allocator );
    } catch ( Exception e ) {
      handleException( e );
    }

    final LoadBalancerDnsRecord dns = LoadBalancers.getDnsRecord(lb);
    if(dns == null || dns.getName() == null)
    	throw new LoadBalancingException("New dns name could not be created");
    
    Function<String, Boolean> rollback = new Function<String, Boolean>(){
    	@Override
    	public Boolean apply(String lbName){
    		try{
    			LoadBalancers.unsetForeignKeys(ctx, lbName);
    		}catch(Exception ex){
    			LOG.warn("unable to unset foreign keys", ex);
    		}

    		try{
        		LoadBalancers.deleteLoadbalancer(ownerFullName, lbName);
        	}catch(LoadBalancingException ex){
        		LOG.error("failed to rollback the loadbalancer: " + lbName, ex);
        		return false;
        	}
    		return true;
    	}
    };
    
    Collection<String> zones = request.getAvailabilityZones().getMember();
    if(zones != null && zones.size()>0){
    	try{
    	LoadBalancers.addZone(lbName, ctx, zones);
    	}catch(LoadBalancingException ex){
    		rollback.apply(lbName);
    		throw ex;
    	}catch(Exception ex){
    		rollback.apply(lbName);
    		throw new LoadBalancingException("Failed to create the loadbalancer (internal error)", ex);
    	}
    }
    
    /// trigger new loadbalancer event 
    try{
    	NewLoadbalancerEvent evt = new NewLoadbalancerEvent();
    	evt.setLoadBalancer(lbName);
    	evt.setContext(ctx);
    	evt.setZones(zones);
    	ActivityManager.getInstance().fire(evt);
    }catch(EventFailedException e){
    	//TODO SPARK: TEST
    	LOG.error("failed to fire new loadbalancer event", e);
    	rollback.apply(lbName);
    	final String reason = e.getCause() != null && e.getCause().getMessage()!=null ? e.getCause().getMessage() : "internal error";
    	throw new LoadBalancingException(String.format("Failed to create the loadbalancer: %s", reason), e);
    }

    Collection<Listener> listeners=request.getListeners().getMember();
    if(listeners!=null && listeners.size()>0){
    	LoadBalancers.createLoadbalancerListener(lbName,  ctx, Lists.newArrayList(listeners));
    	try{
    		CreateListenerEvent evt = new CreateListenerEvent();
    		evt.setLoadBalancer(lbName);
    		evt.setListeners(listeners);
    		evt.setContext(ctx);
    		ActivityManager.getInstance().fire(evt);
    	}catch(EventFailedException e){
    		LOG.error("failed to fire createListener event", e);
        	rollback.apply(lbName);
        	final String reason = e.getCause() != null && e.getCause().getMessage()!=null ? e.getCause().getMessage() : "internal error";
        	throw new LoadBalancingException(String.format("Faild to create the loadbalancer: %s", reason), e);
    	}
    }
    
    final CreateLoadBalancerResult result = new CreateLoadBalancerResult();
    if(dns==null || dns.getDnsName() == null){
    	LOG.warn("No DNS name is assigned to the loadbalancer: "+lbName);
    }
    
    result.setDnsName(dns.getDnsName());
    reply.setCreateLoadBalancerResult(result);
    reply.set_return(true);
    return reply;
  }

  public DescribeLoadBalancersResponseType describeLoadBalancers(DescribeLoadBalancersType request) throws EucalyptusCloudException {
    DescribeLoadBalancersResponseType reply = request.getReply( );
    final Context ctx = Contexts.lookup( );
    final String accountName = ctx.getAccount().getName();
    final Set<String> requestedNames = Sets.newHashSet( );
    if ( !request.getLoadBalancerNames().getMember().isEmpty()) {
      requestedNames.addAll( request.getLoadBalancerNames().getMember() );
    }
    final boolean showAll = requestedNames.remove( "verbose" ) && ctx.hasAdministrativePrivileges();

    Set<LoadBalancer> allowedLBs = null;
    final Function<Set<String>, Set<LoadBalancer>> lookupAccountLBs = new Function<Set<String>, Set<LoadBalancer>>( ) {
          @Override
          public Set<LoadBalancer> apply( final Set<String> identifiers ) {
            final Predicate<? super LoadBalancer> requestedAndAccessible = LoadBalancingMetadatas.filteringFor( LoadBalancer.class )
             .byId( identifiers )
             .byPrivileges( )
             .buildPredicate( );

            final List<LoadBalancer> lbs = Entities.query( LoadBalancer.namedByAccount( accountName , null ), true);
            return Sets.newHashSet( Iterables.filter( lbs, requestedAndAccessible ) );
          }
    };
    
    final Function<Void, Set<LoadBalancer>> lookupAllLBs = new Function<Void, Set<LoadBalancer>>( ) {
        @Override
        public Set<LoadBalancer> apply( final Void arg) {
        	final List<LoadBalancer> lbs = Entities.query( LoadBalancer.named( null , null ), true);
        	return Sets.newHashSet(lbs);
        }
    };
    
    if(showAll)
    	allowedLBs = Entities.asTransaction(LoadBalancer.class, lookupAllLBs).apply(null);
    else
    	allowedLBs = Entities.asTransaction( LoadBalancer.class, lookupAccountLBs ).apply( requestedNames );
    
    final Function<Set<LoadBalancer>, Set<LoadBalancerDescription>> lookupLBDescriptions = new Function<Set<LoadBalancer>, Set<LoadBalancerDescription>> () {
      public Set<LoadBalancerDescription> apply (final Set<LoadBalancer> input){
        final Set<LoadBalancerDescription> descs = Sets.newHashSet();
        for (final LoadBalancer lb : input){
          LoadBalancerDescription desc = new LoadBalancerDescription();
          if(lb==null) // loadbalancer not found
            continue;
          final String lbName = lb.getDisplayName();
          desc.setLoadBalancerName(lbName); /// loadbalancer name
          desc.setCreatedTime(lb.getCreationTimestamp());/// createdtime
          final LoadBalancerDnsRecord dns = lb.getDns();

          desc.setDnsName(dns.getDnsName());           /// dns name
                                            /// instances
          if(lb.getBackendInstances().size()>0){
            desc.setInstances(new Instances());
            desc.getInstances().setMember(new ArrayList<Instance>(
                Collections2.transform(lb.getBackendInstances(), new Function<LoadBalancerBackendInstance, Instance>(){
                  @Override
                  public Instance apply(final LoadBalancerBackendInstance be){
                    Instance instance = new Instance();
                    instance.setInstanceId(be.getInstanceId());
                    return instance;
                  }
                })));
          }
          /// availability zones
          if(lb.getZones().size()>0){
            desc.setAvailabilityZones(new AvailabilityZones());
            final List<LoadBalancerZone> currentZones = 
					Lists.newArrayList(Collections2.filter(lb.getZones(), new Predicate<LoadBalancerZone>(){
						@Override
						public boolean apply(@Nullable LoadBalancerZone arg0) {
							return arg0.getState().equals(LoadBalancerZone.STATE.InService);
						}
			}));
            desc.getAvailabilityZones().setMember(new ArrayList<String>(
                Lists.transform(currentZones, new Function<LoadBalancerZone, String>(){
                  @Override
                  public String apply(final LoadBalancerZone zone){
                    return zone.getName();
                  }
                })));
          }
                                            /// listeners
          if(lb.getListeners().size()>0){
            desc.setListenerDescriptions(new ListenerDescriptions());
            desc.getListenerDescriptions().setMember(new ArrayList<ListenerDescription>(
                Collections2.transform(lb.getListeners(), new Function<LoadBalancerListener, ListenerDescription>(){
                  @Override
                  public ListenerDescription apply(final LoadBalancerListener input){
                    ListenerDescription desc = new ListenerDescription();
                    Listener listener = new Listener();
                    listener.setLoadBalancerPort(input.getLoadbalancerPort());
                    listener.setInstancePort(input.getInstancePort());
                    if(input.getInstanceProtocol() != PROTOCOL.NONE)
                      listener.setInstanceProtocol(input.getInstanceProtocol().name());
                    listener.setProtocol(input.getProtocol().name());
                    if(input.getCertificateId()!=null)
                      listener.setSslCertificateId(input.getCertificateId());
                    desc.setListener(listener);
                    return desc;
                  }
                })));
          }
                                            /// health check
          try{
            int interval = lb.getHealthCheckInterval();
            String target = lb.getHealthCheckTarget();
            int timeout = lb.getHealthCheckTimeout();
            int healthyThresholds = lb.getHealthyThreshold();
            int unhealthyThresholds = lb.getHealthCheckUnhealthyThreshold();

            final HealthCheck hc = new HealthCheck();
            hc.setInterval(interval);
            hc.setHealthyThreshold(healthyThresholds);
            hc.setTarget(target);
            hc.setTimeout(timeout);
            hc.setUnhealthyThreshold(unhealthyThresholds);
            desc.setHealthCheck(hc);
          }catch(IllegalStateException ex){
            ;
          }catch(Exception ex){
            ;
          }
                                            /// backend server description
          									/// source security group
          try{
        	  desc.setSourceSecurityGroup(new SourceSecurityGroup());
        	  LoadBalancerSecurityGroup group = lb.getGroup();
        	  if(group!=null){
        		  desc.getSourceSecurityGroup().setOwnerAlias(group.getGroupOwnerAccountId());
        		  desc.getSourceSecurityGroup().setGroupName(group.getName());
        	  }
          }catch(Exception ex){
        	  ;
          }
         
          descs.add(desc);
        }
        return descs;
      }
    };
    Set<LoadBalancerDescription> descs = lookupLBDescriptions.apply(allowedLBs);

    DescribeLoadBalancersResult descResult = new DescribeLoadBalancersResult();
    LoadBalancerDescriptions lbDescs = new LoadBalancerDescriptions();
    lbDescs.setMember(new ArrayList<LoadBalancerDescription>(descs));
    descResult.setLoadBalancerDescriptions(lbDescs);
    reply.setDescribeLoadBalancersResult(descResult);
    reply.set_return(true);

    return reply;
  }

  public DeleteLoadBalancerResponseType deleteLoadBalancer( DeleteLoadBalancerType request ) throws EucalyptusCloudException {
    DeleteLoadBalancerResponseType reply = request.getReply();
    final String lbToDelete = request.getLoadBalancerName();
    final Context ctx = Contexts.lookup();
    Function<String, LoadBalancer> findLoadBalancer = new Function<String, LoadBalancer>(){
		@Override
		@Nullable
		public LoadBalancer apply(@Nullable String lbName) {
			try{
				LoadBalancer lb = LoadBalancers.getLoadbalancer(ctx, lbName);
				return lb;
			}catch(NoSuchElementException ex){
				if(ctx.hasAdministrativePrivileges()){
					try{
						LoadBalancer lb = LoadBalancers.getLoadBalancerByName(lbName);		
						final User owner = Accounts.lookupUserById(lb.getOwnerUserId());
						ctx.setUser(owner);
						return lb;
					}catch(LoadBalancingException ex2){
						throw Exceptions.toUndeclared(ex2);
					}catch(Exception ex2){
						throw Exceptions.toUndeclared(ex2);
					}
				}
				throw ex;
			}
		}
    };
    
    try {
      if ( lbToDelete != null ) {
        LoadBalancer lb = null;
        try {
          lb = findLoadBalancer.apply(lbToDelete);
        } catch ( NoSuchElementException ex ) {
        	;
        } catch ( Exception ex){
        	if(ex.getCause() != null && ex.getCause() instanceof LoadBalancingException)
        		throw (LoadBalancingException) ex.getCause();
        	else
        		throw ex;
        }
        
        //IAM Support for deleting load balancers
        if (lb != null && ! LoadBalancingMetadatas.filterPrivileged().apply( lb ))
        	throw new AccessPointNotFoundException();
        
        if ( lb != null ) {
          Collection<LoadBalancerListener> listeners = lb.getListeners();
          final List<Integer> ports = Lists.newArrayList( Collections2.transform( listeners, new Function<LoadBalancerListener, Integer>() {
            @Override
            public Integer apply( @Nullable LoadBalancerListener arg0 ) {
              return arg0.getLoadbalancerPort();
            }
          } ) );

          try {
            DeleteListenerEvent evt = new DeleteListenerEvent();
            evt.setLoadBalancer( lbToDelete );
            evt.setContext( ctx );
            evt.setPorts( ports );
            ActivityManager.getInstance().fire( evt );
          } catch ( EventFailedException e ) {
            LOG.error( "failed to fire DeleteListener event", e );
          }

          try {
            DeleteLoadbalancerEvent evt = new DeleteLoadbalancerEvent();
            evt.setLoadBalancer( lbToDelete );
            evt.setContext( ctx );
            ActivityManager.getInstance().fire( evt );
          } catch ( EventFailedException e ) {
            LOG.error( "failed to fire DeleteLoadbalancer event", e );
            throw e;
          }
          
          LoadBalancers.deleteLoadbalancer( UserFullName.getInstance(lb.getOwnerUserId()), lbToDelete );
        }
      }
    }catch (EventFailedException e){
        LOG.error( "Error deleting the loadbalancer: " + e.getMessage(), e );
    	final String reason = e.getCause() != null && e.getCause().getMessage()!=null ? e.getCause().getMessage() : "internal error";
    	throw new LoadBalancingException( String.format("Failed to delete the loadbalancer: %s", reason), e );
    }catch (LoadBalancingException e){
    	throw e;
    }catch ( Exception e ) {
      // success if the lb is not found in the system
      if ( !(e.getCause() instanceof NoSuchElementException) ) {
        LOG.error( "Error deleting the loadbalancer: " + e.getMessage(), e );
        final String reason = "internal error";
        throw new LoadBalancingException( String.format("Failed to delete the loadbalancer: %s", reason), e );
      }
    }
    DeleteLoadBalancerResult result = new DeleteLoadBalancerResult();
    reply.setDeleteLoadBalancerResult( result );
    reply.set_return( true );
    return reply;
  }
  
  public CreateLoadBalancerListenersResponseType createLoadBalancerListeners(CreateLoadBalancerListenersType request) throws EucalyptusCloudException {
	  final CreateLoadBalancerListenersResponseType reply = request.getReply( );
	  final Context ctx = Contexts.lookup( );
	  final String lbName = request.getLoadBalancerName();
	  final List<Listener> listeners = request.getListeners().getMember();

	  
	  LoadBalancer lb = null;
	  try{
	  		lb = LoadBalancers.getLoadbalancer(ctx, lbName);
	  }catch(Exception ex){
	  		throw new AccessPointNotFoundException();
	  }
	  
  	  //IAM support to restricted lb modification
  	  if(lb != null && !LoadBalancingMetadatas.filterPrivileged().apply(lb)) {
	       throw new AccessPointNotFoundException(); // TODO: SPARK: is this the right exception type?
	  }
	  
	  if(listeners!=null)
		  LoadBalancers.validateListener(lb, listeners);
	    
	  try{
    		CreateListenerEvent evt = new CreateListenerEvent();
    		evt.setLoadBalancer(lbName);
    		evt.setContext(ctx);
    		evt.setListeners(listeners);
    		ActivityManager.getInstance().fire(evt);
	  }catch(final EventFailedException e){
    		LOG.error("failed to fire CreateListener event", e);
    		final String reason = e.getCause()!=null && e.getCause().getMessage()!=null ? e.getMessage() : "internal error";
    		throw new LoadBalancingException(String.format("Failed to create listener: %s", reason), e );
	  }
	  LoadBalancers.createLoadbalancerListener(lbName,  ctx, listeners);
	  reply.set_return(true);
	  return reply;
  }
  
  public DeleteLoadBalancerListenersResponseType deleteLoadBalancerListeners(DeleteLoadBalancerListenersType request) throws EucalyptusCloudException {
    final DeleteLoadBalancerListenersResponseType reply = request.getReply( );
    final Context ctx = Contexts.lookup( );
    final String lbName = request.getLoadBalancerName();
    final Collection<Integer> listenerPorts;
    try{
      listenerPorts = Collections2.transform(
        request.getLoadBalancerPorts().getMember(), new Function<String, Integer>(){
          @Override
          public Integer apply(final String input){
            return new Integer(input);
          }
        });
    }catch(Exception ex){
      throw new LoadBalancingException("Invalid port number", ex);
    }

    final LoadBalancer lb;
    try{
      lb = LoadBalancers.getLoadbalancer(ctx, lbName);
    }catch(NoSuchElementException ex){
      throw new AccessPointNotFoundException();
    }catch(Exception ex){
      LOG.error("failed to query loadbalancer due to unknown reason", ex);
      throw new LoadBalancingException("Failed to find the loadbalancer", ex);
    }

   //IAM support to restricted lb modification
   if( lb!=null && !LoadBalancingMetadatas.filterPrivileged().apply(lb) ) {
     throw new AccessPointNotFoundException();
   }

   final Function<Void, Collection<Integer>> filter = new Function<Void, Collection<Integer>>(){
      @Override
      public Collection<Integer> apply(Void v){
         final Collection<Integer> filtered = Sets.newHashSet();
         for(Integer port : listenerPorts){
           final LoadBalancerListener found = lb.findListener(port);
           if(found!=null)
             filtered.add(port);
         }
         return filtered;
      }
    };

    final Collection<Integer> toDelete = Entities.asTransaction(LoadBalancer.class, filter).apply(null);

    final Predicate<Collection<Integer>> remover = new Predicate<Collection<Integer>>(){
      @Override
      public boolean apply(Collection<Integer> listeners){
        for(Integer port : listenerPorts){
          try{
            final LoadBalancerListener exist = Entities.uniqueResult(LoadBalancerListener.named(lb, port));
            Entities.delete(exist);
          }catch(NoSuchElementException ex){
              ;
            }catch(Exception ex){
              LOG.error("Failed to delete the listener", ex);
            }
        }
        return true;
      }
    };

    try{
      DeleteListenerEvent evt = new DeleteListenerEvent();
      evt.setLoadBalancer(lbName);
      evt.setContext(ctx);
      evt.setPorts(listenerPorts);
      ActivityManager.getInstance().fire(evt);
    }catch(EventFailedException e){
      LOG.error("failed to fire DeleteListener event", e);
      final String reason = e.getCause()!=null && e.getCause().getMessage()!=null ? e.getCause().getMessage() : "internal error";
      throw new LoadBalancingException(String.format("Failed to delete listener: %s",reason),e );
    }

    reply.set_return(Entities.asTransaction(LoadBalancerListener.class, remover).apply(toDelete));

    return reply;
  }
  
  public RegisterInstancesWithLoadBalancerResponseType registerInstancesWithLoadBalancer(RegisterInstancesWithLoadBalancerType request) throws EucalyptusCloudException {
	  RegisterInstancesWithLoadBalancerResponseType reply = request.getReply( );
	  final Context ctx = Contexts.lookup( );
	  final UserFullName ownerFullName = ctx.getUserFullName( );
	  final String lbName = request.getLoadBalancerName();
	  final Collection<Instance> instances = request.getInstances().getMember();
	  
	  LoadBalancer lb = null;
	  try{
		  lb= LoadBalancers.getLoadbalancer(ctx, lbName);
	  }catch(final Exception ex){
		  throw new AccessPointNotFoundException();
	  }
	  if( !LoadBalancingMetadatas.filterPrivileged().apply(lb) ) { // IAM policy restriction
		  throw new AccessPointNotFoundException();
	  }
	  
	  final Predicate<LoadBalancer> creator = new Predicate<LoadBalancer>(){
	        @Override
	        public boolean apply( LoadBalancer lb ) {
	        	for(Instance vm : instances){
	        		if(lb.hasBackendInstance(vm.getInstanceId()))
	        			continue;	// the vm instance is already registered
	        		final LoadBalancerBackendInstance beInstance = 
	        				LoadBalancerBackendInstance.newInstance(ownerFullName, lb, vm.getInstanceId());
	        		beInstance.setState(LoadBalancerBackendInstance.STATE.OutOfService);
	        		Entities.persist(beInstance);
	        	}
	        	return true;
	        }
	  };
	  final Function<Void, ArrayList<Instance>> finder = new Function<Void, ArrayList<Instance>>(){
	 	@Override
	  	public ArrayList<Instance> apply(Void v){
	 	  	LoadBalancer lb = null;
	  	  	try{
	  	  		lb=LoadBalancers.getLoadbalancer(ctx, lbName);
	  	  	}catch(Exception ex){
	  	    	LOG.warn("No loadbalancer is found with name="+lbName);    
	  	    	return Lists.newArrayList();
	  	    }
	 	  	Entities.refresh(lb);
	 	  	ArrayList<Instance> result = new ArrayList<Instance>(Collections2.transform(lb.getBackendInstances(), new Function<LoadBalancerBackendInstance, Instance>(){
		    	@Override
		    	public Instance apply(final LoadBalancerBackendInstance input){
		    		final Instance newInst = new Instance();
		    		newInst.setInstanceId(input.getInstanceId());
		    		return newInst;
		    	}}));
	    	return result;
    	}
	 };
	 
	 try{
		 RegisterInstancesEvent evt = new RegisterInstancesEvent();
		 evt.setLoadBalancer(lbName);
    	 evt.setContext(ctx);
    	 evt.setInstances(instances);
    	 ActivityManager.getInstance().fire(evt);
     }catch(EventFailedException e){
    	 LOG.error("failed to fire RegisterInstances event", e);
    	 final String reason = e.getCause()!=null && e.getCause().getMessage()!=null ? e.getCause().getMessage() : "internal error";
    	 throw new LoadBalancingException(String.format("Failed to register instances: %s", reason), e );
     }
	    
	 if(instances!=null){
	    try{
	    	reply.set_return(Entities.asTransaction(LoadBalancerBackendInstance.class, creator).apply(lb));
	    }catch(Exception ex){
	    	throw new LoadBalancingException("Failed to register instances", ex);
	    }
	 }
	    
	 RegisterInstancesWithLoadBalancerResult result = new RegisterInstancesWithLoadBalancerResult();
	 Instances returnInstances = new Instances();
	 returnInstances.setMember(Entities.asTransaction(LoadBalancer.class, finder).apply(null));
	 result.setInstances(returnInstances);
	 reply.setRegisterInstancesWithLoadBalancerResult(result);
	 return reply;
  }

  public DeregisterInstancesFromLoadBalancerResponseType deregisterInstancesFromLoadBalancer(DeregisterInstancesFromLoadBalancerType request) throws EucalyptusCloudException {
	  DeregisterInstancesFromLoadBalancerResponseType reply = request.getReply( );
	  final Context ctx = Contexts.lookup( );
	  final String lbName = request.getLoadBalancerName();
	  final Collection<Instance> instances = request.getInstances().getMember();
	    
	  LoadBalancer lb = null;
	  try{
		  lb = LoadBalancers.getLoadbalancer(ctx, lbName);
	  }catch(Exception ex){
		  throw new AccessPointNotFoundException();
	  }
	  
	  if( lb!=null && !LoadBalancingMetadatas.filterPrivileged().apply(lb) ) { // IAM policy restriction
		  throw new AccessPointNotFoundException();
	  }
	  	 
	  final Function<LoadBalancer, Collection<String>> filter = new Function<LoadBalancer, Collection<String>>(){
	    	@Override
	    	public Collection<String> apply(LoadBalancer lb){
	    		 Collection<String> filtered = Sets.newHashSet();
		   	  	 for(Instance inst : instances){
		   	  		 if(lb.hasBackendInstance(inst.getInstanceId()))
		   	  			 filtered.add(inst.getInstanceId());
		   	  	 }
		   	  	 return filtered;
	    	}
	  };
	    
	  final Collection<String> instancesToRemove = Entities.asTransaction(LoadBalancer.class, filter).apply(lb);
	  if(instancesToRemove==null){
	  	reply.set_return(false);
	  	return reply;
	  }
	  final Predicate<Void> remover = new Predicate<Void>(){
	  	@Override
	  	public boolean apply(Void v){
	      	for(String instanceId : instancesToRemove){
	      	    LoadBalancerBackendInstance toDelete = null;
	      	    try{
	      	    	toDelete = Entities.uniqueResult(LoadBalancerBackendInstance.named(instanceId));
	      	    }catch(NoSuchElementException ex){
	      	    	toDelete=null;
	      	    }catch(Exception ex){
	      	    	LOG.error("Can't query loadbalancer backend instance for "+instanceId);
	      	    	toDelete=null;
	      	    }	
	      	    if(toDelete==null)
	      			continue;
	      		Entities.delete(toDelete);
	      	}
	  	    return true;
	  	}
	  };
	  final Function<Void, ArrayList<Instance>> finder = new Function<Void, ArrayList<Instance>>(){
	  	@Override
	  	public ArrayList<Instance> apply(Void v){
	  	  	 LoadBalancer lb = null;
	  	  	 try{
	  	  		lb= LoadBalancers.getLoadbalancer(ctx, lbName);
	  	  	 }catch(Exception ex){
	   	    	LOG.warn("No loadbalancer is found with name="+lbName);    
	   	    	return Lists.newArrayList();
	   	    }
	  	  	Entities.refresh(lb);
	  	    ArrayList<Instance> result = new ArrayList<Instance>(Collections2.transform(lb.getBackendInstances(), new Function<LoadBalancerBackendInstance, Instance>(){
		  		@Override
		  		public Instance apply(final LoadBalancerBackendInstance input){
		  			final Instance newInst = new Instance();
		  			newInst.setInstanceId(input.getInstanceId());
		  			return newInst;
		  		}}));
	  	    return result;
	  	    }
	  	};
	    
	  try{
		  DeregisterInstancesEvent evt = new DeregisterInstancesEvent();
		  evt.setLoadBalancer(lbName);
		  evt.setContext(ctx);
		  evt.setInstances(instances);
		  ActivityManager.getInstance().fire(evt);
	  }catch(EventFailedException e){
		  LOG.error("failed to fire DeregisterInstances event", e);
    	  final String reason = e.getCause()!=null && e.getCause().getMessage()!=null ? e.getCause().getMessage() : "internal error";
    	  throw new LoadBalancingException(String.format("Failed to deregister instances: %s", reason),e );
	  }
	    
	  reply.set_return(Entities.asTransaction(LoadBalancerBackendInstance.class, remover).apply(null));
	  DeregisterInstancesFromLoadBalancerResult result = new DeregisterInstancesFromLoadBalancerResult();
	  Instances returnInstances = new Instances();
	  returnInstances.setMember(Entities.asTransaction(LoadBalancer.class, finder).apply(null));
	  result.setInstances(returnInstances);
	  reply.setDeregisterInstancesFromLoadBalancerResult(result);
	  return reply;
  }
  
  public EnableAvailabilityZonesForLoadBalancerResponseType enableAvailabilityZonesForLoadBalancer(EnableAvailabilityZonesForLoadBalancerType request) throws EucalyptusCloudException {
	  final EnableAvailabilityZonesForLoadBalancerResponseType reply = request.getReply( );
	  final Context ctx = Contexts.lookup( );
	  final String lbName = request.getLoadBalancerName();
	  final Collection<String> zones = request.getAvailabilityZones().getMember();
	    
	  LoadBalancer lb = null;
	  try{
		  lb = LoadBalancers.getLoadbalancer(ctx, lbName);
	  }catch(final Exception ex){
		  throw new AccessPointNotFoundException();
	  }
		 
	  if( lb!=null && !LoadBalancingMetadatas.filterPrivileged().apply(lb) ) { // IAM policy restriction
		  throw new AccessPointNotFoundException();
	  }
	    
	  if(zones != null && zones.size()>0){
		  try{
			  final EnabledZoneEvent evt = new EnabledZoneEvent();
			  evt.setLoadBalancer(lbName);
			  evt.setContext(ctx);
			  evt.setZones(zones);
			  ActivityManager.getInstance().fire(evt);
		  }catch(EventFailedException e){
			  LOG.error("failed to execute EnabledZone event", e);
	    	  final String reason = e.getCause()!=null && e.getCause().getMessage()!=null ? e.getCause().getMessage() : "internal error";
	    	  throw new LoadBalancingException(String.format("Failed to enable zones: %s", reason),e );
	      }
	  }
	    
	  List<String> availableZones = Lists.newArrayList();
	  try{
  		  LoadBalancer updatedLb = LoadBalancers.getLoadbalancer(ctx, lbName);
  		  availableZones = Lists.transform(LoadBalancers.findZonesInService(updatedLb), new Function<LoadBalancerZone,String>(){
  			 @Override
  			 public String apply(@Nullable LoadBalancerZone arg0) {
				return arg0.getName();
  			 }
  		   });
  	  }catch(Exception ex){
	    	;
	  }
	  final EnableAvailabilityZonesForLoadBalancerResult result = new EnableAvailabilityZonesForLoadBalancerResult();
	  final AvailabilityZones availZones = new AvailabilityZones();
	  availZones.setMember(Lists.newArrayList(availableZones));
	  result.setAvailabilityZones(availZones);
	  reply.setEnableAvailabilityZonesForLoadBalancerResult(result);
  	  reply.set_return(true);
	    
  	  return reply;
  }

  public DisableAvailabilityZonesForLoadBalancerResponseType disableAvailabilityZonesForLoadBalancer(DisableAvailabilityZonesForLoadBalancerType request) throws EucalyptusCloudException {
	  final DisableAvailabilityZonesForLoadBalancerResponseType reply = request.getReply( );
	  final Context ctx = Contexts.lookup( );
	  final String lbName = request.getLoadBalancerName();
	  final Collection<String> zones = request.getAvailabilityZones().getMember();
	
	  LoadBalancer lb = null;
	  try{
		  lb = LoadBalancers.getLoadbalancer(ctx, lbName);
	  }catch(final Exception ex){
		  throw new AccessPointNotFoundException();
	  }
	
	  if( lb!=null && !LoadBalancingMetadatas.filterPrivileged().apply(lb) ) { // IAM policy restriction
		  throw new AccessPointNotFoundException();
	  }
	  
	  if(zones != null && zones.size()>0){
		 try{
	    	final DisabledZoneEvent evt = new DisabledZoneEvent();
	    	evt.setLoadBalancer(lbName);
	    	evt.setContext(ctx);
	    	evt.setZones(zones);
	    	ActivityManager.getInstance().fire(evt);
	     }catch(EventFailedException e){
	    	LOG.error("failed to execute DisabledZone event", e);
	    	final String reason = e.getCause()!=null && e.getCause().getMessage()!=null ? e.getCause().getMessage() : "internal error";
	    	throw new LoadBalancingException(String.format("Failed to disable zones: %s", reason), e );
	    }  
	  }
	  
	  List<String> availableZones = Lists.newArrayList();
	  try{
		  final LoadBalancer updatedLb = LoadBalancers.getLoadbalancer(ctx, lbName);
		  availableZones = Lists.transform(LoadBalancers.findZonesInService(updatedLb), new Function<LoadBalancerZone,String>(){
			  @Override
			  public String apply(@Nullable LoadBalancerZone arg0) {
					return arg0.getName();
					}
		    });
	  }catch(Exception ex){
	    	;
	  }
	  final DisableAvailabilityZonesForLoadBalancerResult result = new DisableAvailabilityZonesForLoadBalancerResult();
  	  final AvailabilityZones availZones = new AvailabilityZones();
  	  availZones.setMember(Lists.newArrayList(availableZones));
  	  result.setAvailabilityZones(availZones);
  	  reply.setDisableAvailabilityZonesForLoadBalancerResult(result);
  	  reply.set_return(true);
	  return reply;
  }
  
  public ConfigureHealthCheckResponseType configureHealthCheck(ConfigureHealthCheckType request) throws EucalyptusCloudException {
    ConfigureHealthCheckResponseType reply = request.getReply( );
    final Context ctx = Contexts.lookup( );
	final String lbName = request.getLoadBalancerName();
	final HealthCheck hc = request.getHealthCheck();
	Integer healthyThreshold = hc.getHealthyThreshold();
	if (healthyThreshold == null)
		throw new LoadBalancingException("Healthy tresholds must be specified");
	Integer interval = hc.getInterval();
	if(interval == null)
		throw new LoadBalancingException("Interval must be specified");
	String target = hc.getTarget();
	if(target == null)
		throw new LoadBalancingException("Target must be specified");
	
	Integer timeout = hc.getTimeout();
    if(timeout==null)
    	throw new LoadBalancingException("Timeout must be specified");
    Integer unhealthyThreshold = hc.getUnhealthyThreshold();
    if(unhealthyThreshold == null)
    	throw new LoadBalancingException("Unhealthy tresholds must be specified");
    LoadBalancer lb = null;
    try{
    	lb = LoadBalancers.getLoadbalancer(ctx, lbName);
    }catch(NoSuchElementException ex){
    	throw new AccessPointNotFoundException();
    }catch(Exception ex){
    	throw new LoadBalancingException("Failed to find the loadbalancer (internal error)");
    }
    
    if( lb!=null && !LoadBalancingMetadatas.filterPrivileged().apply(lb) ) { // IAM policy restriction
	    throw new AccessPointNotFoundException();
    }

    final EntityTransaction db = Entities.get( LoadBalancer.class );
    try{
    	final LoadBalancer update = Entities.uniqueResult(lb);
    	update.setHealthCheck(healthyThreshold, interval, target, timeout, unhealthyThreshold);
		Entities.persist(update);
		db.commit();
    }catch(Exception ex){
    	db.rollback();
    	LOG.error("failed to persist health check config", ex);
    	throw new LoadBalancingException("Failed to persist the health check request (internal error)", ex);
    }
    ConfigureHealthCheckResult result = new ConfigureHealthCheckResult();
    result.setHealthCheck(hc);
    reply.setConfigureHealthCheckResult(result);
    return reply;
  }

  public DescribeInstanceHealthResponseType describeInstanceHealth(DescribeInstanceHealthType request) throws EucalyptusCloudException {
    DescribeInstanceHealthResponseType reply = request.getReply( );
    final Context ctx = Contexts.lookup( );
 	final String lbName = request.getLoadBalancerName();
 	Instances instances = request.getInstances();
 	
	LoadBalancer lb = null;
	try{
		lb= LoadBalancers.getLoadbalancer(ctx, lbName);
	}catch(NoSuchElementException ex){
		throw new AccessPointNotFoundException();
    }catch(Exception ex){
    	throw new LoadBalancingException("Failed to query loadbalancer (internal error)");
    }
 	
    if( lb!=null && !LoadBalancingMetadatas.filterPrivileged().apply(lb) ) { // IAM policy restriction
	    throw new AccessPointNotFoundException();
    }

	List<LoadBalancerBackendInstance> lbInstances = Lists.newArrayList(lb.getBackendInstances());
	List<LoadBalancerBackendInstance> instancesFound = null;
 	if(instances != null && instances.getMember()!= null && instances.getMember().size()>0){
 		instancesFound = Lists.newArrayList();
 		for(Instance inst : instances.getMember()){
 			String instId = inst.getInstanceId();
 			for(final LoadBalancerBackendInstance lbInstance : lbInstances){
 				if(instId.equals(lbInstance.getInstanceId())){
 					instancesFound.add(lbInstance);
 					break;
 				}
 			}
 		}
 	}else{
 		instancesFound = Lists.newArrayList(lb.getBackendInstances());
 	}
 	
 	final ArrayList<InstanceState> stateList = Lists.newArrayList();
 	for(final LoadBalancerBackendInstance instance : instancesFound){
 		InstanceState state = new InstanceState();
 		state.setInstanceId(instance.getDisplayName());
 		state.setState(instance.getState().name());
 		if(instance.getState().equals(LoadBalancerBackendInstance.STATE.OutOfService) && instance.getReasonCode()!=null)
 			state.setReasonCode(instance.getReasonCode());
 		if(instance.getDescription()!=null)
 			state.setDescription(instance.getDescription());
 		
 		stateList.add(state);
 	}
 	
 	final InstanceStates states = new InstanceStates();
 	states.setMember(stateList);
 	final DescribeInstanceHealthResult result = new DescribeInstanceHealthResult();
 	result.setInstanceStates(states);
    reply.setDescribeInstanceHealthResult(result);
    return reply;
  }
  
  ////////////////////////////  2nd-step operations ////////////////////////////

  public SetLoadBalancerListenerSSLCertificateResponseType setLoadBalancerListenerSSLCertificate(SetLoadBalancerListenerSSLCertificateType request) throws EucalyptusCloudException {
    SetLoadBalancerListenerSSLCertificateResponseType reply = request.getReply( );
    return reply;
  }
  
  
  ///////////////////////////   3rd-step operations ////////////////////////////
  public DescribeLoadBalancerPolicyTypesResponseType describeLoadBalancerPolicyTypes(DescribeLoadBalancerPolicyTypesType request) throws EucalyptusCloudException {
	DescribeLoadBalancerPolicyTypesResponseType reply = request.getReply( );
	return reply;
  }

  public DescribeLoadBalancerPoliciesResponseType describeLoadBalancerPolicies(DescribeLoadBalancerPoliciesType request) throws EucalyptusCloudException {
    DescribeLoadBalancerPoliciesResponseType reply = request.getReply( );
    return reply;
  }

  public SetLoadBalancerPoliciesOfListenerResponseType setLoadBalancerPoliciesOfListener(SetLoadBalancerPoliciesOfListenerType request) throws EucalyptusCloudException {
    SetLoadBalancerPoliciesOfListenerResponseType reply = request.getReply( );
    return reply;
  }
  
  public DeleteLoadBalancerPolicyResponseType deleteLoadBalancerPolicy(DeleteLoadBalancerPolicyType request) throws EucalyptusCloudException {
    final DeleteLoadBalancerPolicyResponseType reply = request.getReply( );
 
    return reply;
  }

  public CreateLoadBalancerPolicyResponseType createLoadBalancerPolicy(CreateLoadBalancerPolicyType request) throws EucalyptusCloudException {
    CreateLoadBalancerPolicyResponseType reply = request.getReply( );
    return reply;
  }

  public SetLoadBalancerPoliciesForBackendServerResponseType setLoadBalancerPoliciesForBackendServer(SetLoadBalancerPoliciesForBackendServerType request) throws EucalyptusCloudException {
    SetLoadBalancerPoliciesForBackendServerResponseType reply = request.getReply( );
    return reply;
  }

  public CreateLBCookieStickinessPolicyResponseType createLBCookieStickinessPolicy(CreateLBCookieStickinessPolicyType request) throws EucalyptusCloudException {
    CreateLBCookieStickinessPolicyResponseType reply = request.getReply( );
    return reply;
  }

  public CreateAppCookieStickinessPolicyResponseType createAppCookieStickinessPolicy(CreateAppCookieStickinessPolicyType request) throws EucalyptusCloudException {
    CreateAppCookieStickinessPolicyResponseType reply = request.getReply( );
    return reply;
  }


  //////////////////////// VPC - non support ////////////////////////////////
  public ApplySecurityGroupsToLoadBalancerResponseType applySecurityGroupsToLoadBalancer(ApplySecurityGroupsToLoadBalancerType request) throws EucalyptusCloudException {
    ApplySecurityGroupsToLoadBalancerResponseType reply = request.getReply( );
    return reply;
  }
  
/// VPC - non support
  public AttachLoadBalancerToSubnetsResponseType attachLoadBalancerToSubnets(AttachLoadBalancerToSubnetsType request) throws EucalyptusCloudException {
    AttachLoadBalancerToSubnetsResponseType reply = request.getReply( );
    return reply;
  }
  
/// VPC- non support
  public DetachLoadBalancerFromSubnetsResponseType detachLoadBalancerFromSubnets(DetachLoadBalancerFromSubnetsType request) throws EucalyptusCloudException {
    DetachLoadBalancerFromSubnetsResponseType reply = request.getReply( );
    return reply;
  }

  private static void handleException( final Exception e ) throws LoadBalancingException {
    final LoadBalancingException cause = Exceptions.findCause( e, LoadBalancingException.class );
    if ( cause != null ) {
      throw cause;
    }

    final AuthQuotaException quotaCause = Exceptions.findCause( e, AuthQuotaException.class );
    if ( quotaCause != null ) {
      throw new TooManyAccessPointsException();
    }

    LOG.error( e, e );

    final InternalFailureException exception = new InternalFailureException( String.valueOf(e.getMessage()) );
    if ( Contexts.lookup( ).hasAdministrativePrivileges() ) {
      exception.initCause( e );
    }
    throw exception;
  }
}


